#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage, package and validate the allowlisted game server files."""

import argparse
from collections import Counter
from pathlib import Path, PurePosixPath
import hashlib
import json
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[2]
MAP = json.loads(Path(__file__).with_name("package-map.json").read_text())


def validate_binary(blob, platform):
    if platform == "linux":
        valid = len(blob) >= 52 and blob.startswith(b"\x7fELF\x01\x01\x01")
        valid = valid and struct.unpack_from("<HH", blob, 16) == (3, 3)
    else:
        offset = struct.unpack_from("<I", blob, 60)[0] if len(blob) >= 64 else 0
        valid = blob.startswith(b"MZ") and 64 <= offset <= len(blob) - 26
        if valid:
            signature, machine = struct.unpack_from("<4sH", blob, offset)
            flags, magic = struct.unpack_from("<HH", blob, offset + 22)
            valid = (signature, machine, magic) == (b"PE\0\0", 0x14C, 0x10B)
            valid = valid and bool(flags & 0x2000)
    if not valid:
        raise ValueError(f"Se esperaba un binario compartido {platform} x86.")


def source_digest():
    paths = [ROOT / name for name in (
        "AMBuildScript", "configure.py", "PackageScript",
        "scripts/build/build-native.py", "scripts/deps/build-deps.lock",
        "scripts/package/package-map.json",
    )]
    paths.extend((ROOT / "scripts/build").glob("*.lock.json"))
    for folder in ("extension", "vsp", "addons"):
        directory = ROOT / folder
        paths.extend(path for path in directory.rglob("*") if path.is_file())
    digest = hashlib.sha256()
    for path in sorted(paths):
        digest.update(path.relative_to(ROOT).as_posix().encode("utf-8") + b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def check_paths(paths):
    seen = set()
    for name in paths:
        parts = PurePosixPath(name).parts
        if (not parts or name != "/".join(parts) or name.startswith("/")
                or any(part in {".", ".."} for part in parts)
                or any(character in name for character in '\\:<>"|?*')):
            raise ValueError(f"Ruta no válida en el mapa: {name}")
        key = name.casefold()
        if key in seen:
            raise ValueError(f"Ruta repetida en el mapa: {name}")
        seen.add(key)


def binary_paths(platform):
    suffix = {"linux": ".so", "windows": ".dll"}[platform]
    names = [stem + suffix for stem in MAP["artifact"]["binaries"]]
    check_paths(names)
    return names


def reject_link(path):
    if path.is_symlink() or (sys.version_info >= (3, 12) and path.is_junction()):
        raise ValueError(f"No se admiten enlaces en el paquete: {path}")


def file_bytes(base, name):
    path = base
    reject_link(path)
    for part in PurePosixPath(name).parts:
        path /= part
        reject_link(path)
    if not path.is_file():
        raise ValueError(f"Falta un archivo de producto: {path}")
    return path.read_bytes()


def binary_hashes(build, platform):
    hashes = {}
    for name in binary_paths(platform):
        blob = file_bytes(build / "package", name)
        validate_binary(blob, platform)
        hashes[name] = hashlib.sha256(blob).hexdigest()
    return hashes


def git_commit():
    git = shutil.which("git")
    if git is None or not (ROOT / ".git").exists():
        return None
    status = subprocess.run([git, "status", "--porcelain"], cwd=ROOT, capture_output=True, text=True)
    if status.returncode or status.stdout:
        return None
    head = subprocess.run([git, "rev-parse", "HEAD"], cwd=ROOT, capture_output=True, text=True)
    return head.stdout.strip() if head.returncode == 0 else None


def record_build(build, platform, digest):
    receipt = {
        "platform": platform,
        "sources": digest,
        "commit": git_commit(),
        "binaries": binary_hashes(build, platform),
    }
    with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=build, delete=False) as output:
        pending = Path(output.name)
        json.dump(receipt, output, sort_keys=True)
    try:
        pending.replace(build / ".build-complete")
    finally:
        pending.unlink(missing_ok=True)


def products(build, platform, *, with_receipt=False):
    artifact = MAP["artifact"]
    check_paths([*binary_paths(platform), *artifact["resources"], *artifact["documentation"]])
    check_paths(artifact["documentation"].values())
    receipt_path = build / ".build-complete"
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError) as error:
        raise ValueError(f"Ejecuta make build-exts-{platform} para generar el recibo de build.") from error
    if not isinstance(receipt, dict) or receipt.get("platform") != platform or receipt.get("sources") != source_digest():
        raise ValueError(f"El build quedó obsoleto. Ejecuta make build-exts-{platform}.")

    files = {}
    expected = receipt.get("binaries")
    if not isinstance(expected, dict) or expected.keys() != set(binary_paths(platform)):
        raise ValueError("El recibo de build no contiene el inventario esperado.")
    for name, digest in expected.items():
        blob = file_bytes(build / "package", name)
        validate_binary(blob, platform)
        if hashlib.sha256(blob).hexdigest() != digest:
            raise ValueError(f"El binario difiere del recibo de build: {name}")
        files[name] = blob

    for name in artifact["resources"]:
        blob = file_bytes(build / "package", name)
        if blob != file_bytes(ROOT, name) or (not blob and not name.endswith(".autoload")):
            raise ValueError(f"El recurso del build falta, está vacío o difiere de su fuente: {name}")
        files[name] = blob

    for name, source in artifact["documentation"].items():
        blob = file_bytes(ROOT, source)
        if not blob.strip():
            raise ValueError(f"Documento obligatorio vacío: {source}")
        files[name] = blob
    validate_notices(files)
    return (files, receipt) if with_receipt else files


def validate_notices(files):
    license_text = files["LICENSE"]
    notice = files["NOTICE.md"]
    if b"GNU GENERAL PUBLIC LICENSE" not in license_text or b"END OF TERMS AND CONDITIONS" not in license_text:
        raise ValueError("LICENSE debe incluir el texto completo de la GPL.")
    expected = (b"Boost Software License", b"Zyan Disassembler Library", b"Zyan Core Library")
    if any(marker not in notice for marker in expected):
        raise ValueError("NOTICE.md debe incluir los avisos de SafetyHook, Zydis y Zycore.")


def validate_stage(stage, files):
    reject_link(stage)
    actual = {path.relative_to(stage).as_posix() for path in stage.rglob("*")
              if not path.is_dir() or path.is_symlink()}
    if actual != files.keys():
        raise ValueError(f"Inventario de staging incorrecto. Diferencias: {sorted(actual ^ files.keys())}")
    for name, blob in files.items():
        if file_bytes(stage, name) != blob:
            raise ValueError(f"Staging desactualizado: {name}")


def validate_zip(path, files):
    with zipfile.ZipFile(path) as archive:
        if Counter(archive.namelist()) != Counter(files.keys()):
            raise ValueError("El ZIP contiene entradas ajenas, ausentes o repetidas.")
        for entry in archive.infolist():
            mode = stat.S_IFMT(entry.external_attr >> 16)
            if mode not in (0, stat.S_IFREG):
                raise ValueError(f"El ZIP contiene una entrada que no es un archivo normal: {entry.filename}")
            if archive.read(entry) != files[entry.filename]:
                raise ValueError(f"El ZIP difiere del build: {entry.filename}")


def release_documents(files, receipt, platform, channel):
    commit = receipt.get("commit")
    if commit != git_commit():
        commit = None
    if os.environ.get("GITHUB_ACTIONS") == "true" and commit != os.environ["GITHUB_SHA"]:
        raise ValueError("El recibo debe corresponder al commit limpio de CI.")
    if commit and re.fullmatch(r"[0-9a-f]{40}", commit):
        revision = (
            f"[{commit}](https://github.com/AoC-Gamers/SourceTV-Support/tree/{commit})"
        )
    else:
        revision = f"Fuentes locales SHA-256 `{receipt['sources']}`"
    identity = f"**Distribución** `{channel}` · {platform} x86 · {revision}"
    documents = {}
    for name in ("README.md", "CHANGELOG.md"):
        title, _, body = files[name].decode("utf-8").partition("\n")
        documents[name] = f"{title}\n\n{identity}\n{body}".encode("utf-8")
    return documents


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("stage", "release", "validate"))
    parser.add_argument("platform", choices=("linux", "windows"))
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--dist", type=Path, required=True)
    parser.add_argument("--channel", required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"(?:develop|latest|[0-9a-f]{7,12}|[0-9]+\.[0-9]+(?:\.[0-9]+)?(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?)", args.channel):
        parser.error("Canal, SHA corto o versión no válidos.")

    stage = args.dist / args.platform / "artifact"
    archive = args.dist / "release" / f"{MAP['name']}-{args.channel}-{args.platform}.zip"
    files, receipt = products(args.build, args.platform, with_receipt=True)
    if args.action == "stage":
        stage.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".stage-", dir=stage.parent) as work:
            pending = Path(work) / "artifact"
            for name, blob in files.items():
                target = pending / name
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(blob)
            validate_stage(pending, files)
            if stage.exists():
                shutil.rmtree(stage)
            pending.rename(stage)
        print(stage)
        return

    validate_stage(stage, files)
    documents = release_documents(files, receipt, args.platform, args.channel)
    archive_files = {**files, **documents}
    if args.action == "release":
        archive.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".release-", dir=archive.parent) as work:
            pending = Path(work) / archive.name
            with zipfile.ZipFile(pending, "w", compression=zipfile.ZIP_DEFLATED) as output:
                for name in sorted(files):
                    if name in documents:
                        output.writestr(name, documents[name])
                    else:
                        output.write(stage / name, name)
            validate_zip(pending, archive_files)
            pending.replace(archive)
    else:
        validate_zip(archive, archive_files)
    print(archive)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        sys.exit(str(error))
