# SPDX-License-Identifier: GPL-3.0-only
"""Measure source-bound product line coverage locally and validate its receipt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import make_sonar

ROOT = Path(__file__).resolve().parents[2]
AREAS = (*make_sonar.SOURCE_DIRECTORIES, "scripts", "tests", "addons")
NATIVE_FLAGS = "-m32 -std=c++17 -O0 --coverage -fprofile-abs-path"
SUITES = {
    "SourceTV-Manager": {"test-native", "recording_sessions", "hook_context"},
    "SourceTV-Support": {"test-native", "demo_payload", "transmission_policy", "runtime_lifecycle"},
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_state(root):
    paths = [root / name for name in ("Makefile", "AMBuildScript", "configure.py")]
    paths.extend(p for area in AREAS for p in (root / area).rglob("*")
                 if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc")
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(set(paths)) if p.is_file()}


def read_gcov(path):
    return json.loads(subprocess.check_output(
        ["gcov", "--json-format", "--stdout", str(path)], cwd=ROOT, text=True))


def own_source(root, name, directories):
    path = (root / name).resolve()
    return path.is_file() and any(path.is_relative_to(root / area) for area in directories)


def merge_gcov(document, lines, *, baseline=False):
    working = Path(document["current_working_directory"])
    for entry in document["files"]:
        path = (working / entry["file"]).resolve()
        if not own_source(ROOT, path, make_sonar.SOURCE_DIRECTORIES):
            continue
        rows = lines.setdefault(path.relative_to(ROOT).as_posix(), {})
        length = len(path.read_bytes().splitlines())
        for line in entry["lines"]:
            number, count = line["line_number"], line["count"]
            if not 1 <= number <= length or count < 0 or (baseline and count):
                raise ValueError(f"Invalid gcov line or counter in {path.name}")
            rows[number] = rows.get(number, False) or count > 0


def native_lines(output, database, env):
    log = output / "native-tests.log"
    with log.open("w") as stream:
        subprocess.run(["make", "--no-print-directory", "test-native", f"NATIVE_CXXFLAGS={NATIVE_FLAGS}"],
                       cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
    location = next((line[8:] for line in log.read_text().splitlines() if line.startswith("Output: ")), "")
    test_dir = Path(location).resolve()
    if test_dir.parent != ROOT / ".build" or not test_dir.name.startswith("native."):
        raise ValueError("Missing directory for instrumented native tests")
    required = SUITES["SourceTV-Support" if (ROOT / "vsp").is_dir() else "SourceTV-Manager"]
    counters = sorted(test_dir.glob("*.gcda"))
    if len(counters) != len(required) or any(
        not any(counter.stem == name or counter.stem.startswith(name + "-") for counter in counters)
        for name in required
    ):
        raise ValueError("Missing or unexpected counters in instrumented native tests")
    for binary in required:
        header = (test_dir / binary).read_bytes()[:20]
        if header[:5] != b"\x7fELF\x01" or header[18:20] != b"\x03\x00":
            raise ValueError("Native test is not ELF x86")

    lines = {}
    for counter in counters:
        merge_gcov(read_gcov(counter), lines)
    # Collect unexecuted lines from the same product translation units and flags.
    units = make_sonar.compilation_database(ROOT, database)
    with (output / "native-baseline.log").open("w") as stream:
        for index, entry in enumerate(units):
            obj = output / f"unit-{index}.o"
            args = iter(entry["arguments"])
            command = []
            for arg in args:
                if arg in {"-o", "-MF", "-MT", "-MQ"}:
                    next(args)
                elif arg not in {"-MD", "-MMD", "-MP"}:
                    command.append(arg)
            subprocess.run([*command, "--coverage", "-fprofile-abs-path", "-O0", "-o", str(obj)],
                           cwd=entry["directory"], env=env, stdout=stream, stderr=subprocess.STDOUT, check=True)
            merge_gcov(read_gcov(obj.with_suffix(".gcno")), lines, baseline=True)
    return lines, {"flags": NATIVE_FLAGS, "units": len(units),
                   "counter_sha256": {p.name: digest(p) for p in counters}}


def python_lines(output, python, env):
    rc = output / "coverage.ini"
    directories = sorted({p.parent.relative_to(ROOT).as_posix() for p in (ROOT / "scripts").rglob("*.py")})
    rc.write_text("[run]\nsource = " + ", ".join(directories) + "\npatch = subprocess\nparallel = True\n")
    env = {**env, "COVERAGE_RCFILE": str(rc), "COVERAGE_FILE": str(output / ".coverage")}
    command = [python, "-m", "coverage"]
    with (output / "python-tests.log").open("w") as stream:
        for args in (["run", "-m", "unittest", "discover", "-s", "tests/package"],
                     ["combine"], ["json", "-o", str(output / "python.json")]):
            subprocess.run([*command, *args], cwd=ROOT, env=env, stdout=stream,
                           stderr=subprocess.STDOUT, check=True)
    report = json.loads((output / "python.json").read_text())
    return {name: {number: hit for hit, key in ((False, "missing_lines"), (True, "executed_lines"))
                   for number in record[key]}
            for name, record in report["files"].items() if own_source(ROOT, name, ("scripts",))}, {
                "version": report["meta"]["version"], "subprocess": True}


def write_report(path, lines):
    if not any(any(rows.values()) for rows in lines.values()):
        raise ValueError("No executed lines in product scope")
    xml = ET.Element("coverage", version="1")
    for name, rows in sorted(lines.items()):
        if not rows:
            continue
        file = ET.SubElement(xml, "file", path=name)
        for number, hit in sorted(rows.items()):
            ET.SubElement(file, "lineToCover", lineNumber=str(number), covered=str(hit).lower())
    ET.indent(xml)
    ET.ElementTree(xml).write(path, encoding="utf-8", xml_declaration=True)


def validate_receipt(root, receipt, database):
    receipt = (root / receipt).resolve()
    record = json.loads(receipt.read_text())
    if (record.get("schema") != 1 or record.get("sources") != source_state(root)
            or record.get("compile_database_sha256") != digest(database)):
        raise ValueError("Coverage source state or compilation database changed")
    paths = []
    for kind, dirs in (("native", make_sonar.SOURCE_DIRECTORIES), ("python", ("scripts",))):
        entry = record["reports"][kind]
        path = receipt.parent / f"{kind}.xml"
        if digest(path) != entry["sha256"]:
            raise ValueError(f"Coverage report changed: {kind}")
        xml = ET.parse(path).getroot()
        if xml.tag != "coverage" or xml.get("version") != "1":
            raise ValueError(f"Invalid coverage XML: {kind}")
        files = set()
        for file in xml:
            name = file.get("path", "")
            if file.tag != "file" or name in files or not own_source(root, name, dirs):
                raise ValueError("Coverage includes duplicate or non-product files")
            files.add(name)
            length = len((root / name).read_bytes().splitlines())
            numbers = set()
            for row in file:
                number = int(row.get("lineNumber", "0"))
                if (row.tag != "lineToCover" or not 1 <= number <= length or number in numbers
                        or row.get("covered") not in {"true", "false"}):
                    raise ValueError("Invalid coverage line")
                numbers.add(number)
        if not files:
            raise ValueError(f"Empty coverage XML: {kind}")
        paths.append(str(path))
    return paths


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-commands", required=True, type=Path)
    parser.add_argument("--coverage-python", default=sys.executable)
    parser.add_argument("--validate", type=Path)
    args = parser.parse_args()
    database = args.compile_commands.resolve()
    if args.validate:
        print("\n".join(validate_receipt(ROOT, args.validate, database)))
        return
    directory = ROOT / ".build" / "coverage"
    directory.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=directory))
    print(f"Coverage output: {output}", flush=True)
    state = source_state(ROOT)
    context = digest(database)
    env = {key: val for key, val in os.environ.items()
           if not key.startswith(("SONAR_", "COVERAGE_")) and key not in {"MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES"}}
    reports = {}
    for kind, (lines, inputs) in (("native", native_lines(output, database, env)),
                                  ("python", python_lines(output, args.coverage_python, env))):
        path = output / f"{kind}.xml"
        write_report(path, lines)
        total = sum(len(rows) for rows in lines.values())
        covered = sum(sum(rows.values()) for rows in lines.values())
        reports[kind] = {"sha256": digest(path), "covered_lines": covered,
                         "lines": total, "inputs": inputs}
        print(f"{kind}: {covered}/{total} product lines")
    if state != source_state(ROOT) or context != digest(database):
        raise ValueError("Coverage inputs changed during execution")
    receipt = output / "receipt.json"
    receipt.write_text(json.dumps({"schema": 1, "sources": state, "compile_database_sha256": context,
                                   "reports": reports}, indent=2) + "\n")
    validate_receipt(ROOT, receipt, database)
    print(f"SONAR_COVERAGE_RECEIPT={receipt}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, ET.ParseError, subprocess.CalledProcessError) as error:
        sys.exit(f"Coverage failed: {error}")
