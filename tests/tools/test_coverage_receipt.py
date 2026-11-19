# SPDX-License-Identifier: GPL-3.0-only
import json
import os
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts/tools"))
import local_coverage as coverage
import make_sonar


class CoverageReceiptTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        for folder in (*make_sonar.SOURCE_DIRECTORIES, "scripts", "tests/native",
                       "output", ".deps/sdk with space", ".deps/python/coverage"):
            (self.root / folder).mkdir(parents=True)
        (self.root / ".deps/python/coverage/__init__.py").write_text("__version__ = 'fixture'\n")
        self.header = self.root / ".deps/sdk with space/api.h"
        self.header.write_text("#define RESULT 41\n")
        self.source = self.root / "extension/product.cpp"
        self.source.write_text('#include "api.h"\nint product() { return RESULT; }\n')
        self.test = self.root / "tests/native/fixture.cpp"
        self.test.write_text("int main() { return 0; }\n")
        (self.root / "scripts/product.py").write_text("x = 1\n")
        self.db = self.root / "compile_commands.json"
        self.db.write_text(json.dumps([{
            "directory": str(self.root), "file": str(self.source),
            "arguments": ["g++", "-m32", "-DSE_LEFT4DEAD2=15", "-DSOURCE_ENGINE=15",
                          "-I", str(self.header.parent), "-c", str(self.source), "-o", "product.o"]
        }]))
        self.profile = {"python": sys.executable, "cxx": "g++", "gcov": "gcov",
                        "flags": "-m32 -std=c++17", "tests": "fixture:.:fixture"}
        env = patch.dict(os.environ, {"PYTHONPATH": str(self.root / ".deps/python")})
        env.start()
        self.addCleanup(env.stop)
        self.context, _, _ = coverage.coverage_context(
            self.root, self.db, self.profile, coverage.coverage_environment())
        self.receipt = self.root / "output/receipt.json"
        reports = {}
        for kind, path in (("native", "extension/product.cpp"), ("python", "scripts/product.py")):
            report = self.root / "output" / f"{kind}.xml"
            coverage.write_report(report, {path: {1: True}})
            reports[kind] = {"sha256": coverage.digest(report)}
        self.record = {"schema": 2, "sources": coverage.source_state(self.root),
                       "compile_database_sha256": coverage.digest(self.db), "profile": self.profile,
                       "context": self.context, "reports": reports}
        self.write_receipt()

    def write_receipt(self):
        self.receipt.write_text(json.dumps(self.record))

    def validate(self):
        return coverage.validate_receipt(self.root, self.receipt, self.db)

    def test_transitive_header_and_link_target(self):
        self.assertEqual(len(self.validate()), 2)
        self.header.write_text("#define RESULT 42\n")
        with self.assertRaisesRegex(ValueError, "dependency or toolchain context changed"):
            self.validate()
        self.header.write_text("#define RESULT 41\n")
        self.assertEqual(len(self.validate()), 2)
        replacement = self.header.with_name("same.h")
        replacement.write_bytes(self.header.read_bytes())
        self.header.unlink()
        self.header.symlink_to(replacement.name)
        with self.assertRaisesRegex(ValueError, "dependency or toolchain context changed"):
            self.validate()

    def test_profile_compiler_and_report(self):
        self.profile["flags"] += " -O2"
        self.write_receipt()
        with self.assertRaisesRegex(ValueError, "dependency or toolchain context changed"):
            self.validate()
        self.profile["flags"] = "-m32 -std=c++17"
        self.write_receipt()
        gcov = self.root / "gcov"
        gcov.write_text("#!/bin/sh\nprintf 'test gcov override\\n'\n")
        gcov.chmod(0o700)
        with patch.dict(os.environ, {"PATH": f"{self.root}:{os.environ['PATH']}"}):
            with self.assertRaisesRegex(ValueError, "dependency or toolchain context changed"):
                self.validate()
        report = self.receipt.parent / "native.xml"
        report.write_text(report.read_text() + "\n")
        with self.assertRaisesRegex(ValueError, "Coverage report changed"):
            self.validate()

    def test_test_source_and_compile_options(self):
        initial = self.test.read_text()
        self.test.write_text(initial + "\n")
        with self.assertRaisesRegex(ValueError, "source state or compilation database changed"):
            self.validate()
        self.test.write_text(initial)
        self.db.write_text(self.db.read_text().replace("sdk with space", "another sdk"))
        with self.assertRaisesRegex(ValueError, "source state or compilation database changed"):
            self.validate()

    def test_concurrent_change_invalidates_measurement(self):
        def measure_native(*_):
            self.header.write_text("#define RESULT 43\n")
            return {"extension/product.cpp": {1: True}}, {}

        args = ["local_coverage", "--compile-commands", str(self.db), "--coverage-python",
                sys.executable, "--native-flags=-m32 -std=c++17", "--native-tests=fixture:.:fixture"]
        with (patch.object(coverage, "ROOT", self.root), patch.object(sys, "argv", args),
              patch.object(coverage, "native_lines", measure_native),
              patch.object(coverage, "python_lines", return_value=({"scripts/product.py": {1: True}}, {}))):
            with self.assertRaisesRegex(ValueError, "inputs changed during execution"):
                coverage.main()
        self.assertFalse(list((self.root / ".build/coverage").glob("*/receipt.json")))

    def test_runtime_coverage_validates_counters_and_matching_build(self):
        build = self.root / "runtime"
        binary = build / "package/product.so"
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"fixture")
        baseline = {"sources": "snapshot", "platform": "linux", "dependencies": {}}
        baseline["binaries"] = {binary.name: coverage.digest(binary)}
        (self.root / ".build-complete").write_text(json.dumps(baseline))
        runtime_receipt = build / ".build-complete"
        runtime_receipt.write_text(json.dumps({**baseline, "coverage": True}))

        entry, = json.loads(self.db.read_text())
        entry["directory"] = str(build)
        entry["arguments"].extend(coverage.RUNTIME_FLAGS)
        entry["output"] = "product.o"
        runtime_db = build / self.db.name
        runtime_db.write_text(json.dumps([entry]))
        counter = build / "product.gcda"
        notes = counter.with_suffix(".gcno")
        notes.write_bytes(b"notes")
        counter.write_bytes(b"counter")
        counters, inputs = coverage.runtime_inputs(self.root, self.db, build)
        self.assertEqual(counters, [counter])
        self.record["runtime"] = {"build": str(build), "inputs": inputs}
        self.write_receipt()
        self.assertEqual(len(self.validate()), 2)

        counter.write_bytes(b"other counter")
        with self.assertRaisesRegex(ValueError, "Runtime coverage inputs changed"):
            self.validate()
        counter.unlink()
        with self.assertRaisesRegex(ValueError, "Missing runtime counter"):
            self.validate()
        counter.write_bytes(b"counter")

        entry["arguments"].append("-O2")
        runtime_db.write_text(json.dumps([entry]))
        with self.assertRaisesRegex(ValueError, "differs beyond coverage flags"):
            self.validate()
        entry["arguments"].pop()
        runtime_db.write_text(json.dumps([entry]))

        binary.write_bytes(b"other binary")
        with self.assertRaisesRegex(ValueError, "Instrumented binary changed"):
            self.validate()
        binary.write_bytes(b"fixture")
        baseline["sources"] = "obsolete"
        runtime_receipt.write_text(json.dumps({**baseline, "coverage": True}))
        with self.assertRaisesRegex(ValueError, "Instrumented build differs in sources"):
            self.validate()

    def test_runtime_gcov_rejects_mismatched_data(self):
        source = self.root / "extension/product.cpp"
        source.write_text("int main() { return 0; }\n")
        binary = self.root / "product"
        subprocess.run(["g++", "-m32", *coverage.RUNTIME_FLAGS, str(source), "-o", str(binary)],
                       cwd=self.root, check=True)
        subprocess.run([str(binary)], cwd=self.root, check=True)
        counter, = self.root.glob("*.gcda")
        lines = {}
        with patch.object(coverage, "ROOT", self.root):
            coverage.runtime_lines([counter], "gcov", coverage.coverage_environment(), lines)
            self.assertEqual(lines, {"extension/product.cpp": {1: True}})
            notes = counter.with_suffix(coverage.GCOV_NOTES_SUFFIX)
            notes.unlink()
            with self.assertRaisesRegex(ValueError, "Runtime gcov rejected"):
                coverage.runtime_lines([counter], "gcov", coverage.coverage_environment(), {})

    def test_scanner_only_receives_current_reports(self):
        cppcheck = self.root / "cppcheck"
        cppcheck.write_text("#!/bin/sh\necho '<results><errors/></results>' >&2\n")
        scanner = self.root / "scanner"
        scanner.write_text("#!/bin/sh\nprintf '%s\\n' \"$@\" > scanner-arguments\n")
        for executable in (cppcheck, scanner):
            executable.chmod(0o700)
        env = {"SONAR_HOST_URL": "http://localhost", "SONAR_TOKEN": "test-only",
               "SONAR_CPPCHECK": str(cppcheck), "SONAR_SCANNER": str(scanner),
               "SONAR_COVERAGE_RECEIPT": str(self.receipt), "SONAR_COMPILE_COMMANDS": str(self.db)}
        with patch.dict(os.environ, env):
            make_sonar.run("analyze", self.db, self.root)
            invoked = self.root / "scanner-arguments"
            self.assertIn("-Dsonar.coverageReportPaths=" + ",".join(self.validate()), invoked.read_text())
            invoked.unlink()
            self.header.write_text("#define RESULT 42\n")
            with self.assertRaisesRegex(make_sonar.SonarError, "Coverage receipt rejected"):
                make_sonar.run("analyze", self.db, self.root)
            self.assertFalse(invoked.exists())
