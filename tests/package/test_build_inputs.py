# SPDX-License-Identifier: GPL-3.0-only
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/package"))
import artifact_contract
import provenance


class BuildInputsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.repo = self.root / "dependency"
        self.repo.mkdir()
        self.git(self.repo, "init", "--quiet", "--object-format=sha1")
        (self.repo / "input.h").write_text("locked\n", encoding="utf-8")
        (self.repo / ".gitignore").write_text("ignored.h\n", encoding="utf-8")
        self.git(self.repo, "add", ".")
        self.commit(self.repo)

    @staticmethod
    def git(path, *args):
        return subprocess.check_output(
            ["git", "-C", path, "-c", "protocol.file.allow=always", *args],
            stderr=subprocess.PIPE, text=True,
        ).strip()

    def commit(self, path):
        self.git(path, "-c", "user.name=Test", "-c", "user.email=test@example.com",
                 "commit", "--quiet", "--message=fixture")

    def test_locked_checkout_and_wrong_revision(self):
        revision = self.git(self.repo, "rev-parse", "HEAD")
        identity = provenance.checkout_identity(self.repo, revision)
        self.assertEqual(identity["revision"], revision)
        self.assertRegex(identity["sha256"], r"^[0-9a-f]{64}$")
        with self.assertRaisesRegex(ValueError, "se esperaba"):
            provenance.checkout_identity(self.repo, "0" * 40)
        with self.assertRaisesRegex(ValueError, "checkout Git"):
            provenance.checkout_identity(self.root, revision)

    def test_changed_and_untracked_inputs(self):
        revision = self.git(self.repo, "rev-parse", "HEAD")
        header = self.repo / "input.h"
        header.write_text("modified\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "cambios locales"):
            provenance.checkout_identity(self.repo, revision)
        self.git(self.repo, "checkout", "--", "input.h")
        for name in ("local.h", "ignored.h"):
            with self.subTest(name=name):
                other = self.repo / name
                other.touch()
                with self.assertRaisesRegex(ValueError, name):
                    provenance.checkout_identity(self.repo, revision)
                other.unlink()
        self.git(self.repo, "update-index", "--assume-unchanged", "input.h")
        with self.assertRaisesRegex(ValueError, "índice oculta"):
            provenance.checkout_identity(self.repo, revision)

    def test_nested_submodule_changed_or_missing(self):
        parent = self.root / "parent"
        parent.mkdir()
        self.git(parent, "init", "--quiet", "--object-format=sha1")
        self.git(parent, "submodule", "add", "--quiet", str(self.repo), "vendor/code")
        self.commit(parent)
        revision = self.git(parent, "rev-parse", "HEAD")
        provenance.checkout_identity(parent, revision)
        child = parent / "vendor/code"
        header = child / "input.h"
        header.write_text("modified\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "cambios locales"):
            provenance.checkout_identity(parent, revision)
        self.git(child, "checkout", "--", "input.h")
        self.git(parent, "submodule", "deinit", "--force", "--", "vendor/code")
        with self.assertRaisesRegex(ValueError, "checkout Git"):
            provenance.checkout_identity(parent, revision)

    def test_failed_build_removes_receipt_before_compiler(self):
        tools = artifact_contract.ROOT / "scripts/build/build-native.py"
        spec = importlib.util.spec_from_file_location("build_native", tools)
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)

        output = self.root / "output"
        output.mkdir()
        receipt = output / ".build-complete"
        receipt.write_text("previous success", encoding="utf-8")
        stale = output / "stale.so"
        stale.write_bytes(b"old binary")
        platform = "windows" if os.name == "nt" else "linux"
        argv = ["build-native.py", platform, "--build", str(output), "--venv", str(self.root)]
        for kind in ("sourcemod", "metamod", "hl2sdk", "ambuild"):
            argv.extend([f"--{kind}", str(self.repo)])
        if hasattr(provenance, "steamworks_identity"):
            argv.extend(["--steamworks", str(self.repo)])
        with (patch.object(sys, "argv", argv),
              patch.object(build, "dependency_identities", return_value={}),
              patch.object(build.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "compiler"))):
            with self.assertRaises(subprocess.CalledProcessError):
                build.main()
        self.assertFalse(receipt.exists())
        self.assertEqual(stale.read_bytes(), b"old binary")


    def test_steamworks_lock_accepts_line_endings_and_receipt_tracks_bytes(self):
        sdk = self.root / "sdk"
        steam = sdk / "public/steam"
        steam.mkdir(parents=True)
        header = steam / "steam_api.h"
        lock = self.root / "scripts/build/steamworks-headers.lock.json"
        lock.parent.mkdir(parents=True)
        locked = b"locked header\n"
        lock.write_text(json.dumps({header.name: hashlib.sha256(locked).hexdigest()}),
                        encoding="utf-8")
        with patch.object(provenance, "ROOT", self.root):
            header.write_bytes(locked)
            lf = provenance.steamworks_identity(sdk)
            header.write_bytes(locked.replace(b"\n", b"\r\n"))
            crlf = provenance.steamworks_identity(sdk)
            self.assertNotEqual(lf["sha256"], crlf["sha256"])
            header.write_bytes(b"wrong header\n")
            with self.assertRaisesRegex(ValueError, r"steam_api\.h difiere"):
                provenance.steamworks_identity(sdk)
