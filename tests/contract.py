# SPDX-License-Identifier: GPL-3.0-only
"""Check the SourceMod and VSP entry points against the frozen baseline."""

import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = Path(__file__).with_name("fixtures") / "public-contract.json"


def inventory(read):
    extension = "\n".join(read(source) for source in (
        "extension/extension.cpp",
        "extension/hooks/detours.cpp",
        "extension/hooks/runtime_hooks.cpp",
    ))
    vsp = re.sub(r"//[^\n]*", "", read("vsp/plugin_vsp.cpp"))
    cvars = re.search(r'\bcvars\[\]\s*=\s*\{([^}]+)\}', vsp)
    return {
        "libraries": re.findall(r'RegisterLibrary\([^,]+,\s*"([^"]+)"', extension),
        "native_registrations": re.findall(r'\bAddNatives\(([^;]+)\);', extension),
        "engine_cvars": [name for declaration in re.findall(r'\bConVarRef\s+([^;]+);', extension)
                         for name in re.findall(r'"([^"]+)"', declaration)],
        "vsp_entry_points": re.findall(r'EXPOSE_SINGLE_INTERFACE\([^;]+\);', vsp),
        "vsp_cvars": re.findall(r'"([^"]+)"', cvars.group(1)) if cvars else [],
    }


class PublicContractTest(unittest.TestCase):
    def test_frozen_public_api(self):
        expected = json.loads(FIXTURE.read_text(encoding="utf-8"))["contract"]
        self.assertEqual(inventory(lambda name: (ROOT / name).read_text(encoding="utf-8-sig")), expected)


if __name__ == "__main__":
    unittest.main()
