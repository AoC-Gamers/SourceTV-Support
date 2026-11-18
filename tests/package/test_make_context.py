# SPDX-License-Identifier: GPL-3.0-only
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MakeContextTest(unittest.TestCase):
    def test_sanitized_recipe_uses_selected_sources_from_another_repository(self):
        with tempfile.TemporaryDirectory() as folder:
            caller = Path(folder)
            subprocess.run(["git", "init", "-q"], cwd=caller, check=True)
            subprocess.run(
                ["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "caller", "--allow-empty"],
                cwd=caller, check=True,
            )
            (caller / "Makefile").write_text("test-native:\n\t@echo WRONG_MAKEFILE\n")
            project = caller / "source snapshot"
            project.mkdir()
            shutil.copyfile(ROOT / "Makefile", project / "Makefile")
            compiler = caller / "compiler"
            compiler.write_text(
                '#!/bin/bash\nset -eu\n'
                'if [[ "$1" == --version ]]; then echo test-compiler; exit; fi\n'
                'while [[ "$1" != -o ]]; do shift; done\n'
                'printf "#!/bin/sh\\necho ran-native-suite\\n" > "$2"\n'
                'chmod +x "$2"\n'
            )
            compiler.chmod(0o755)

            command = ["make", "-f", str(project / "Makefile"),
                       "test-native-sanitized", "CXX=./compiler"]
            result = subprocess.run(command, cwd=caller, capture_output=True, text=True, check=True)
            self.assertNotIn("WRONG_MAKEFILE", result.stdout)
            self.assertIn("Source SHA unavailable", result.stdout)
            self.assertIn("-fsanitize=address,undefined", result.stdout)
            self.assertGreater(result.stdout.count("ran-native-suite"), 1)

            subprocess.run(["git", "init", "-q"], cwd=project, check=True)
            subprocess.run(
                ["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                 "commit", "-qm", "project", "--allow-empty"],
                cwd=project, check=True,
            )
            expected = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], cwd=project, text=True,
            ).strip()
            (caller / "Makefile").unlink()
            result = subprocess.run(command, cwd=caller, capture_output=True, text=True, check=True)
            self.assertIn(expected, result.stdout.splitlines())

            compiler.write_text("#!/bin/sh\nexit 1\n")
            result = subprocess.run(command, cwd=caller, capture_output=True, text=True)
            self.assertNotEqual(0, result.returncode)
