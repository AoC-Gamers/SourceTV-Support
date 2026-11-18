# SPDX-License-Identifier: GPL-3.0-only
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/package"))
import artifact_contract


class PublishPreflightTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.sha = "a" * 40
        self.slug = artifact_contract.load_map()["name"]
        self.assets = self.root / "dist/release"
        self.assets.mkdir(parents=True)
        self.commands = self.root / "calls"
        tools = self.root / "tools"
        tools.mkdir()
        for command, body in {
            "git": 'printf "git %s\\n" "$*" >>"$CALLS"\nprintf "%s\\n" "$GITHUB_SHA"',
            "gh": 'printf "gh %s\\n" "$*" >>"$CALLS"\nexit 1',
            "date": 'printf "date %s\\n" "$*" >>"$CALLS"\nprintf "2026-11-18T00:00:00Z\\n"',
        }.items():
            shim = tools / command
            shim.write_text(f"#!/usr/bin/env bash\n{body}\n", encoding="utf-8")
            shim.chmod(0o755)
        self.env = {**os.environ, "GITHUB_SHA": self.sha, "GITHUB_REF_TYPE": "branch",
                    "GITHUB_REF_NAME": "develop", "GITHUB_REF": "refs/heads/develop",
                    "CALLS": str(self.commands),
                    "PATH": str(tools) + os.pathsep + os.environ["PATH"]}

    def asset(self, platform):
        return self.assets / f"{self.slug}-develop-{platform}.zip"

    def write_asset(self, platform, *, channel="develop", sha=None):
        with ZipFile(self.asset(platform), "w") as archive:
            archive.writestr("README.md", f"`{channel}` · {platform} x86 · [{sha or self.sha}]")

    def publish(self):
        script = artifact_contract.ROOT / "scripts/ci/publish-release.sh"
        result = subprocess.run(["bash", str(script), self.slug, "develop"], cwd=self.root,
                                env=self.env, capture_output=True, text=True, encoding="utf-8")
        self.assertNotEqual(result.returncode, 0)
        return result

    def test_missing_and_empty_assets_report_name_before_git_or_gh(self):
        for platform, other in (("linux", "windows"), ("windows", "linux")):
            with self.subTest(platform=platform):
                self.write_asset(other)
                self.assertIn(self.asset(platform).name, self.publish().stderr)
                self.asset(platform).touch()
                self.assertIn(self.asset(platform).name, self.publish().stderr)
                self.asset(platform).unlink()
                self.asset(other).unlink()
        self.assertFalse(self.commands.exists())

    def test_wrong_channel_or_commit_rejected_before_remote_access(self):
        self.write_asset("linux")
        for identity in ({"channel": "latest"}, {"sha": "b" * 40}):
            with self.subTest(identity=identity):
                self.write_asset("windows", **identity)
                self.assertIn("Identidad de release inesperada", self.publish().stderr)
        self.assertEqual(self.commands.read_text(encoding="utf-8").splitlines(), ["git rev-parse HEAD"] * 2)
