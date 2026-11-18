# SPDX-License-Identifier: GPL-3.0-only
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
            binaries = contract_tool.binary_paths("linux")
            elf = bytearray(52)
            elf[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<HH", elf, 16, 3, 3)
            for name in binaries:
                path = build / "package" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(elf)
            with patch.object(contract_tool, "source_digest", return_value="current"):
                contract_tool.record_build(build, "linux", "current")
                (build / "package" / binaries[0]).write_bytes(elf + b"other binary")
                with self.assertRaisesRegex(ValueError, "difiere del recibo"):
                    contract_tool.products(build, "linux")

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
