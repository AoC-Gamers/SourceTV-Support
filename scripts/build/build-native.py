#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the requested x86 platform with AMBuild."""

import argparse
import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts/package"))
from artifact_contract import load_map, record_build, source_digest, validate_map
from provenance import add_input_arguments, dependency_identities


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("platform", choices=("linux", "windows"))
    for option in ("build", "venv"):
        parser.add_argument(f"--{option}", type=Path, required=True)
    add_input_arguments(parser)
    args = parser.parse_args()

    build = args.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / ".build-complete"
    receipt.unlink(missing_ok=True)
    if sys.platform != {"linux": "linux", "windows": "win32"}[args.platform]:
        raise ValueError(f"El build {args.platform} requiere un host {args.platform}.")
    mapping = load_map()
    validate_map(mapping)
    dependencies = dependency_identities(args)
    windows = args.platform == "windows"
    env_bin = args.venv.resolve() / ("Scripts" if windows else "bin")
    python = env_bin / ("python.exe" if windows else "python")
    target = mapping["build"]
    config = [
        python, "-B", "-S", ROOT / "configure.py", "--targets", target["arch"], "--enable-optimize",
        "--sdks", ",".join(target["sdks"]),
        "--generate-compile-commands",
        "--mms-path", args.metamod.resolve(), "--sm-path", args.sourcemod.resolve(),
    ]

    env = {
        **os.environ,
        "HL2SDKL4D2": str(args.hl2sdk.resolve()),
        "PYTHONPATH": str(args.ambuild.resolve()),
        "PYTHONNOUSERSITE": "1",
        "PYTHONDONTWRITEBYTECODE": "1",
    }
    config += [
        "--hl2sdk-manifest-path", args.sourcemod.resolve() / "hl2sdk-manifests",
        "--swsdk-path", args.steamworks.resolve(),
    ]

    digest = source_digest()
    subprocess.run(config, cwd=build, env=env, check=True)
    subprocess.run(
        [python, "-B", "-S", "-c",
         "import sys; sys.path.insert(0, sys.argv.pop(1)); "
         "from ambuild2.run import cli_run; raise SystemExit(cli_run())",
         args.ambuild.resolve()],
        cwd=build, env=env, check=True,
    )
    if source_digest() != digest:
        raise ValueError("Las fuentes cambiaron durante la compilación. Repite el build.")
    if dependency_identities(args) != dependencies:
        raise ValueError("Las dependencias cambiaron durante la compilación. Repite el build.")
    record_build(build, args.platform, digest, dependencies, mapping)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
