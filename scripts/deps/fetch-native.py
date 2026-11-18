#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare the locked native toolchain without modifying existing checkouts."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
GITHUB = "https://github.com/alliedmodders"
REPOSITORIES = {
    "HL2SDK_COMMIT": ("hl2sdk", "hl2sdk-l4d2", False),
    "SOURCEMOD_COMMIT": ("sourcemod", "sourcemod-1.12", True),
    "MMSOURCE_COMMIT": ("metamod-source", "mmsource-1.12", True),
    "AMBUILD_COMMIT": ("ambuild", "ambuild", False),
}


def run(*args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def submodules(path):
    run("git", "-C", path, "submodule", "update", "--init", "--recursive", "--depth", "1")


def checkout(base, revision, spec):
    repo, name, recursive = spec
    target = base / name
    if target.exists():
        actual = run("git", "-C", target, "rev-parse", "HEAD", capture_output=True, text=True)
        if actual.stdout.strip() != revision:
            raise ValueError(f"{target} no está en {revision}. Usa otro DEPS_DIR.")
        return

    with tempfile.TemporaryDirectory(prefix=f".{name}-", dir=base) as work:
        path = Path(work) / name
        run("git", "init", "--quiet", path)
        run("git", "-C", path, "remote", "add", "origin", f"{GITHUB}/{repo}.git")
        run("git", "-C", path, "fetch", "--depth", "1", "origin", revision)
        run("git", "-C", path, "checkout", "--quiet", "--detach", "FETCH_HEAD")
        if recursive:
            submodules(path)
        path.rename(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--deps", type=Path, required=True)
    parser.add_argument("--venv", type=Path, required=True)
    args = parser.parse_args()

    deps = args.deps.resolve()
    deps.mkdir(parents=True, exist_ok=True)
    lock = dict(line.split("=", 1) for line in
                (ROOT / "scripts/deps/build-deps.lock").read_text().splitlines()
                if line and not line.startswith("#"))
    for key, spec in REPOSITORIES.items():
        if key in lock:
            checkout(deps, lock[key], spec)
    venv = args.venv.resolve()
    python = venv / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")
    stamp = venv / ".ambuild-revision"
    revision = lock["AMBUILD_COMMIT"]
    if python.is_file() and stamp.is_file() and stamp.read_text().strip() == revision:
        return
    if not python.is_file():
        run(sys.executable, "-m", "venv", venv)
    # Install from a copy so pip never modifies the locked checkout or creates dirt.
    with tempfile.TemporaryDirectory(prefix="aoc-ambuild-") as work:
        source = Path(work) / "ambuild"
        shutil.copytree(deps / "ambuild", source, ignore=shutil.ignore_patterns(".git"))
        run(python, "-m", "pip", "install", "--disable-pip-version-check", source)
    stamp.write_text(revision + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
