# SPDX-License-Identifier: GPL-3.0-only
from copy import deepcopy
import os
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/package"))
import artifact_contract as contract_tool
from artifact_contract import validate_binary, validate_stage, validate_zip


MAP = contract_tool.load_map()


class ArtifactContractTest(unittest.TestCase):
    def test_binary_platform_and_architecture(self):
        elf = bytearray(52)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        struct.pack_into("<HH", elf, 16, 3, 3)
        validate_binary(elf, "linux")
        elf[4] = 2
        with self.assertRaises(ValueError):
            validate_binary(elf, "linux")

        pe = bytearray(90)
        pe[:2] = b"MZ"
        struct.pack_into("<I", pe, 60, 64)
        struct.pack_into("<4sH", pe, 64, b"PE\0\0", 0x14C)
        struct.pack_into("<HH", pe, 86, 0x2000, 0x10B)
        validate_binary(pe, "windows")
        struct.pack_into("<H", pe, 68, 0x8664)
        with self.assertRaises(ValueError):
            validate_binary(pe, "windows")

    def test_replaced_binary_fails_despite_matching_platform(self):
        with tempfile.TemporaryDirectory() as work:
            build = Path(work)
            binaries = contract_tool.binary_paths("linux", MAP)
            elf = bytearray(52)
            elf[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<HH", elf, 16, 3, 3)
            for name in binaries:
                path = build / "package" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(elf)
            with (patch.object(contract_tool, "source_digest", return_value="current"),
                  patch.object(contract_tool, "dependency_identities", return_value={"sdk": "locked"})):
                contract_tool.record_build(build, "linux", "current", {"sdk": "locked"}, MAP)
                (build / "package" / binaries[0]).write_bytes(elf + b"other binary")
                with self.assertRaisesRegex(ValueError, "difiere del recibo"):
                    contract_tool.products(build, "linux", None, MAP)

    def test_changed_dependency_invalidates_receipt(self):
        with tempfile.TemporaryDirectory() as work:
            build = Path(work)
            inputs = object()
            with (patch.object(contract_tool, "source_digest", return_value="current"),
                  patch.object(contract_tool, "binary_hashes", return_value={}),
                  patch.object(contract_tool, "dependency_identities", return_value={"sdk": "changed"}) as verify):
                contract_tool.record_build(build, "linux", "current", {"sdk": "locked"}, MAP)
                with self.assertRaisesRegex(ValueError, "dependencias del recibo"):
                    contract_tool.products(build, "linux", inputs, MAP)
                verify.assert_called_once_with(inputs)

    def test_map_rejects_unknown_keys_at_every_level(self):
        for section in ((), ("build",), ("artifact",), ("artifact", "documentation")):
            with self.subTest(section=section):
                mapping = deepcopy(MAP)
                current = mapping
                for key in section:
                    current = current[key]
                current["typo"] = True
                with self.assertRaisesRegex(ValueError, "claves inesperadas.*typo"):
                    contract_tool.validate_map(mapping)
        with self.assertRaisesRegex(ValueError, "clave duplicada"):
            contract_tool.unique_map_keys([("arch", "x86"), ("arch", "x86")])

    def test_map_rejects_missing_invalid_and_colliding_entries(self):
        cases = [
            lambda m: m["build"].pop("sdks"),
            lambda m: m["build"].update(sdks="l4d2"),
            lambda m: m["artifact"].update(resources={"required": True}),
            lambda m: m.update(name="../product"),
            lambda m: m["artifact"]["resources"].append(m["artifact"]["resources"][0].upper()),
            lambda m: m["artifact"]["resources"].pop(),
            lambda m: m["artifact"]["binaries"].pop(),
            lambda m: m["artifact"]["documentation"].update({"LICENSE": "missing.md"}),
            lambda m: m["artifact"]["documentation"].update({"README.md": "docs/installation/README.md"}),
        ]
        for index, modify in enumerate(cases):
            with self.subTest(case=index):
                mapping = deepcopy(MAP)
                modify(mapping)
                with self.assertRaises(ValueError):
                    contract_tool.validate_map(mapping)

    def test_zip_readme_keeps_canonical_content_and_resolves_omitted_docs(self):
        mapping = MAP
        self.assertEqual(mapping["artifact"]["documentation"]["README.md"], "README.md")
        sha = "a" * 40
        documented = b"# Product\n\n[Build](docs/build/README.md#ci) and [include](addons/example.inc).\n"
        with (patch.object(contract_tool, "git_commit", return_value=sha),
              patch.dict(os.environ, {"GITHUB_SHA": sha})):
            docs = contract_tool.release_documents(
                {"README.md": documented, "CHANGELOG.md": b"# Changelog\n"},
                {"commit": sha, "sources": "current"}, "linux", "develop",
            )
        readme = docs["README.md"].decode()
        self.assertIn(f"`develop` · linux x86 · [{sha}]", readme)
        self.assertIn(f"[Build]({contract_tool.REPOSITORY}/blob/{sha}/docs/build/README.md#ci)", readme)
        self.assertIn("[include](addons/example.inc)", readme)

    def test_missing_legal_notice_is_rejected(self):
        license_text = (contract_tool.ROOT / "LICENSE").read_bytes()
        with self.assertRaisesRegex(ValueError, "NOTICE"):
            contract_tool.validate_notices({"LICENSE": license_text, "NOTICE.md": b"TODO"})

    def test_stage_and_archive_allowlist_and_contents(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            stage = root / "artifact"
            stage.mkdir()
            source = root / "source"
            source.write_text("expected")
            path = stage / "LICENSE"
            path.write_text("expected")
            contract = {"LICENSE": source.read_bytes()}
            validate_stage(stage, contract)
            extra = stage / ".env.secret"
            extra.touch()
            with self.assertRaises(ValueError):
                validate_stage(stage, contract)
            extra.unlink()
            path.write_text("modified")
            with self.assertRaises(ValueError):
                validate_stage(stage, contract)

            archive = root / "package.zip"
            with zipfile.ZipFile(archive, "w") as output:
                output.write(source, "LICENSE")
            validate_zip(archive, contract)
            with zipfile.ZipFile(archive, "a") as output:
                output.write(source, "../LICENSE")
            with self.assertRaises(ValueError):
                validate_zip(archive, contract)


if __name__ == "__main__":
    unittest.main()
