# SPDX-License-Identifier: GPL-3.0-only
"""Verify the inputs of the native build and describe them in its receipt."""

import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess


ROOT = Path(__file__).resolve().parents[2]
LOCK_PATH = Path(__file__).with_name("build-deps.lock")
NATIVE_REVISIONS = {
    "sourcemod": "SOURCEMOD_COMMIT",
    "metamod": "MMSOURCE_COMMIT",
    "hl2sdk": "HL2SDK_COMMIT",
    "ambuild": "AMBUILD_COMMIT",
}


def read_lock():
    return dict(line.split("=", 1) for line in LOCK_PATH.read_text().splitlines()
                if line and not line.startswith("#"))


def git(path, *args):
    result = subprocess.run(
        ["git", "--no-replace-objects", "--no-optional-locks", "-C", path, "-c", "core.fsmonitor=false",
         "-c", "core.ignorestat=false", *args],
        capture_output=True,
    )
    if result.returncode:
        raise ValueError(f"{path}: Git no pudo verificar la dependencia: "
                         f"{os.fsdecode(result.stderr).strip()}")
    return result.stdout


def tree_entries(path, revision):
    for entry in git(path, "ls-tree", "-rz", revision).split(b"\0"):
        if entry:
            metadata, name = entry.split(b"\t", 1)
            mode, _, oid = metadata.split()
            yield mode, oid.decode("ascii"), name


def blob_id(data):
    header = f"blob {len(data)}\0".encode("ascii")
    digest = hashlib.sha1(header, usedforsecurity=False)
    digest.update(data)
    return digest.hexdigest()


def checkout_identity(path, revision):
    path = path.resolve()
    if not (path / ".git").exists():
        raise ValueError(f"{path}: se requiere un checkout Git completo y verificable.")
    actual = git(path, "rev-parse", "HEAD").strip().decode("ascii")
    if actual != revision:
        raise ValueError(f"{path}: revisión {actual}, se esperaba {revision}.")
    index = git(path, "ls-files", "-v", "-z").split(b"\0")
    if any(entry and not entry.startswith(b"H ") for entry in index):
        raise ValueError(f"{path}: el índice oculta archivos o tiene conflictos.")
    if git(path, "status", "--porcelain", "-z", "--untracked-files=no", "--ignore-submodules=all"):
        raise ValueError(f"{path}: hay cambios locales en archivos versionados.")
    extras = git(path, "ls-files", "--others", "-z")
    if extras:
        extra_name = os.fsdecode(extras.split(b"\0", 1)[0])
        raise ValueError(f"{path}: archivo ajeno al checkout: {extra_name}.")

    digest = hashlib.sha256()
    for mode, oid, name in tree_entries(path, revision):
        entry = path / os.fsdecode(name)
        digest.update(mode + b" " + name + b"\0")
        if mode == b"160000":
            digest.update(bytes.fromhex(checkout_identity(entry, oid)["sha256"]))
        else:
            # Reject links even if tracked: our locked SDKs only need normal files.
            if not stat.S_ISREG(entry.lstat().st_mode) or entry.resolve() != entry:
                raise ValueError(f"{entry}: se requiere un archivo normal dentro del checkout.")
            data = entry.read_bytes()
            if blob_id(data) != oid and blob_id(data.replace(b"\r\n", b"\n")) != oid:
                raise ValueError(f"{entry}: el contenido difiere del objeto fijado por Git.")
            digest.update(hashlib.sha256(data).digest())
    return {"revision": revision, "sha256": digest.hexdigest()}


def add_input_arguments(parser):
    for name in (*NATIVE_REVISIONS, "steamworks"):
        parser.add_argument(f"--{name}", type=Path, required=True)


def dependency_identities(args):
    lock = read_lock()
    identities = {name: checkout_identity(getattr(args, name), lock[key])
                  for name, key in NATIVE_REVISIONS.items()}
    identities["steamworks"] = steamworks_identity(args.steamworks)
    return identities


def steamworks_identity(sdk):
    sdk = sdk.resolve()
    expected = json.loads(
        (ROOT / "scripts/build/steamworks-headers.lock.json").read_text(encoding="utf-8")
    )
    public = sdk / "public"
    headers = public / "steam"
    if (public.resolve() != public or headers.resolve() != headers
            or not headers.is_dir() or set(public.iterdir()) != {headers}
            or {p.name for p in headers.glob("*.h")} != expected.keys()):
        raise ValueError("Configura STEAMWORKS_SDK_DIR con los headers fijados del SDK 1.55.")
    digest = hashlib.sha256()
    for path in sorted(headers.rglob("*")):
        mode = path.lstat().st_mode
        name = path.relative_to(headers).as_posix()
        if path.resolve() != path:
            raise ValueError(f"Steamworks SDK 1.55: no se admiten enlaces: {name}.")
        if stat.S_ISDIR(mode):
            continue
        if not stat.S_ISREG(mode):
            raise ValueError(f"Steamworks SDK 1.55: {name} debe ser un archivo normal.")
        data = path.read_bytes()
        if name in expected and hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest() != expected[name]:
            raise ValueError(f"Steamworks SDK 1.55: el header {name} difiere de la revisión fijada.")
        digest.update(name.encode("utf-8") + b"\0" + hashlib.sha256(data).digest())
    return {"revision": "1.55", "sha256": digest.hexdigest()}
