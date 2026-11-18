#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the requested x86 platform with AMBuild."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/package"))
from artifact_contract import MAP, record_build, source_digest


def validate_steamworks(sdk):
    expected = json.loads(Path(__file__).with_name("steamworks-headers.lock.json").read_text())
    if {p.name for p in (sdk / "public/steam").glob("*.h")} != expected.keys():
        raise ValueError("Configura STEAMWORKS_SDK_DIR con los headers del SDK 1.55 de AoC.")
    for name, digest in expected.items():
        data = (sdk / "public/steam" / name).read_bytes().replace(b"\r\n", b"\n")
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError(f"Steamworks SDK 1.55: el header {name} difiere de la revisión fijada.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("linux", "windows"))
    for option in ("build", "sourcemod", "metamod", "hl2sdk", "venv", "steamworks"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    args = parser.parse_args()

    build = args.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / ".build-complete"
    receipt.unlink(missing_ok=True)
    if sys.platform != {"linux": "linux", "windows": "win32"}[args.platform]:
        raise ValueError(f"El build {args.platform} requiere un host {args.platform}.")
    windows = args.platform == "windows"
    env_bin = args.venv.resolve() / ("Scripts" if windows else "bin")
    python = env_bin / ("python.exe" if windows else "python")
    ambuild = env_bin / ("ambuild.exe" if windows else "ambuild")
    target = MAP["build"]
    config = [
        python, ROOT / "configure.py", "--targets", target["arch"], "--enable-optimize",
        "--sdks", ",".join(target["sdks"]),
        "--generate-compile-commands",
        "--mms-path", args.metamod.resolve(), "--sm-path", args.sourcemod.resolve(),
    ]

    env = {**os.environ, "HL2SDKL4D2": str(args.hl2sdk.resolve())}
    validate_steamworks(args.steamworks)
    config += [
        "--hl2sdk-manifest-path", args.sourcemod.resolve() / "hl2sdk-manifests",
        "--swsdk-path", args.steamworks.resolve(),
    ]

    digest = source_digest()
    subprocess.run(config, cwd=build, env=env, check=True)
    subprocess.run([ambuild], cwd=build, env=env, check=True)
    if source_digest() != digest:
        raise ValueError("Las fuentes cambiaron durante la compilación. Repite el build.")
    record_build(build, args.platform, digest)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
