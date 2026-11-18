# SPDX-License-Identifier: GPL-3.0-only
"""Measure source-bound product line coverage locally and validate its receipt."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

import make_sonar

ROOT = Path(__file__).resolve().parents[2]
AREAS = (*make_sonar.SOURCE_DIRECTORIES, "scripts", "tests", "addons")
COVERAGE_FLAGS = ["--coverage", "-fprofile-abs-path"]
BUILD_ENVIRONMENT = ("PATH", "GCC_EXEC_PREFIX", "COMPILER_PATH", "LIBRARY_PATH", "CPATH",
                     "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "DEPENDENCIES_OUTPUT",
                     "SUNPRO_DEPENDENCIES", "SOURCE_DATE_EPOCH", "PYTHONPATH", "PYTHONHOME")


def tool(command, directory, env):
    name = command if "/" not in command else str(directory / command)
    found = shutil.which(name, path=env.get("PATH"))
    if not found:
        raise ValueError(f"Coverage tool unavailable: {command}")
    return str(Path(found).absolute())


def identify(path):
    return {"path": path, "target": str(Path(path).resolve()), "sha256": digest(Path(path))}


def query(command, directory, env):
    result = subprocess.run(command, cwd=directory, env=env, text=True, capture_output=True)
    if result.returncode:
        raise ValueError(f"Coverage input query failed: {command[0]}\n{result.stderr}")
    return result.stdout


def baseline_command(entry):
    args = iter(entry["arguments"])
    command = []
    for arg in args:
        if arg in {"-o", "-MF", "-MT", "-MQ"}:
            next(args)
        elif arg not in {"-MD", "-MMD", "-MP"}:
            command.append(arg)
    return [*command, *COVERAGE_FLAGS, "-O0"]


def headers(command, directory, env):
    output = query([arg for arg in command if arg != "-c"] + ["-M", "-MT", "coverage"],
                   directory, env)
    prefix = "coverage:"
    if not output.startswith(prefix):
        raise ValueError("Invalid GCC dependency output")
    dependencies = output[len(prefix):].replace("\\\n", " ")
    for word in re.findall(r"(?:\\.|[^\s\\])+", dependencies):
        name = re.sub(r"\\([ #\\\t])", r"\1", word).replace("$$", "$")
        yield Path(os.path.abspath(directory / name))


def coverage_context(root, database, profile, env):
    gcov = tool(profile["gcov"], root, env)
    python = tool(profile["python"], root, env)
    cxx = shlex.split(profile["cxx"])
    if len(cxx) != 1:
        raise ValueError("Coverage requires CXX to name a single GCC executable")
    driver = tool(cxx[0], root, env)
    flags = shlex.split(profile["flags"]) + COVERAGE_FLAGS
    if "-m32" not in flags:
        raise ValueError("Native coverage requires -m32")
    suites = []
    for record in profile["tests"].split():
        source, include, binary = record.split(":")
        command = [driver, *flags, f"-I{root / 'extension' / include}",
                   str(root / "tests/native" / f"{source}.cpp")]
        suites.append((binary, command))

    units = []
    for entry in make_sonar.compilation_database(root, database):
        command = baseline_command(entry)
        directory = Path(entry["directory"])
        command[0] = tool(command[0], directory, env)
        units.append((directory, command))

    inputs = set()
    compilers = {}
    for directory, command in [*units, *((root, cmd) for _, cmd in suites)]:
        inputs.update(headers(command, directory, env))
        executable = command[0]
        if executable not in compilers:
            tools = {}
            for name in ("cc1", "cc1plus", "as", "ld", "collect2"):
                reported = query([executable, "-m32", f"-print-prog-name={name}"],
                                 directory, env).strip()
                tools[name] = identify(tool(reported, directory, env))
            compilers[executable] = {
                **identify(executable), "version": query([executable, "--version"], directory, env),
                "tools": tools,
            }

    coverage_package = query([python, "-c", "import coverage; print(coverage.__file__); print(coverage.__version__)"],
                             root, env).splitlines()
    package_dir = Path(coverage_package[0]).parent
    submodules = []
    if (root / ".gitmodules").is_file():
        status = query(["git", "submodule", "status", "--recursive"], root, env)
        # Keep the revision and path, independent of local tag names.
        submodules = [line.split(" (", 1)[0] for line in status.splitlines()]
    context = {
        "environment": {name: env.get(name) for name in BUILD_ENVIRONMENT},
        "compilers": compilers,
        "submodules": submodules,
        "gcov": {**identify(gcov), "version": query([gcov, "--version"], root, env)},
        "units": [[str(directory), command] for directory, command in units],
        "native_tests": [[binary, command] for binary, command in suites],
        "python": {**identify(python), "version": query([python, "--version"], root, env),
                   "coverage_version": coverage_package[1],
                   "coverage_files": {str(p): digest(p) for p in sorted(package_dir.rglob("*"))
                                      if p.is_file() and "__pycache__" not in p.parts}},
        "headers": {str(p): {"target": str(p.resolve()), "sha256": digest(p)} for p in sorted(inputs)},
    }
    return context, units, suites


def coverage_environment():
    return {key: val for key, val in os.environ.items()
            if not key.startswith(("SONAR_", "COVERAGE_"))
            and key not in {"MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES", "GNUMAKEFLAGS"}}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_state(root):
    paths = [root / name for name in ("Makefile", "AMBuildScript", "configure.py", ".gitmodules")]
    paths.extend(p for area in AREAS for p in (root / area).rglob("*")
                 if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc")
    return {p.relative_to(root).as_posix(): digest(p) for p in sorted(set(paths)) if p.is_file()}


def read_gcov(path, gcov, env):
    return json.loads(subprocess.check_output(
        [gcov, "--json-format", "--stdout", str(path)], cwd=ROOT, env=env, text=True))


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


def native_lines(output, suites, units, gcov, env):
    log = output / "native-tests.log"
    with log.open("w") as stream:
        for binary, command in suites:
            subprocess.run([*command, "-o", str(output / binary)], cwd=ROOT, env=env,
                           stdout=stream, stderr=subprocess.STDOUT, check=True)
            subprocess.run([str(output / binary)], cwd=ROOT, env=env,
                           stdout=stream, stderr=subprocess.STDOUT, check=True)
    required = {binary for binary, _ in suites}
    counters = sorted(output.glob("*.gcda"))
    if len(counters) != len(required) or any(
        not any(counter.stem == name or counter.stem.startswith(name + "-") for counter in counters)
        for name in required
    ):
        raise ValueError("Missing or unexpected counters in instrumented native tests")
    for binary in required:
        header = (output / binary).read_bytes()[:20]
        if header[:5] != b"\x7fELF\x01" or header[18:20] != b"\x03\x00":
            raise ValueError("Native test is not ELF x86")

    lines = {}
    for counter in counters:
        merge_gcov(read_gcov(counter, gcov, env), lines)
    # Collect unexecuted lines from the same product translation units and flags.
    with (output / "native-baseline.log").open("w") as stream:
        for index, (directory, command) in enumerate(units):
            obj = output / f"unit-{index}.o"
            subprocess.run([*command, "-o", str(obj)], cwd=directory, env=env,
                           stdout=stream, stderr=subprocess.STDOUT, check=True)
            merge_gcov(read_gcov(obj.with_suffix(".gcno"), gcov, env), lines, baseline=True)
    return lines, {"units": len(units),
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
    if (record.get("schema") != 2 or record.get("sources") != source_state(root)
            or record.get("compile_database_sha256") != digest(database)):
        raise ValueError("Coverage source state or compilation database changed")
    context, _, _ = coverage_context(root, database, record["profile"], coverage_environment())
    if context != record.get("context"):
        raise ValueError("Coverage dependency or toolchain context changed")
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
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--gcov", default="gcov")
    parser.add_argument("--native-flags")
    parser.add_argument("--native-tests")
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
    if not args.native_tests or not args.native_flags:
        parser.error("run make test-coverage to select native suites and flags")
    profile = {"cxx": args.cxx, "gcov": args.gcov, "flags": args.native_flags,
               "tests": args.native_tests, "python": tool(args.coverage_python, ROOT, os.environ)}
    state = source_state(ROOT)
    database_hash = digest(database)
    env = coverage_environment()
    context, units, suites = coverage_context(ROOT, database, profile, env)
    reports = {}
    for kind, (lines, inputs) in (
        ("native", native_lines(output, suites, units, context["gcov"]["path"], env)),
        ("python", python_lines(output, profile["python"], env)),
    ):
        path = output / f"{kind}.xml"
        write_report(path, lines)
        total = sum(len(rows) for rows in lines.values())
        covered = sum(sum(rows.values()) for rows in lines.values())
        reports[kind] = {"sha256": digest(path), "covered_lines": covered,
                         "lines": total, "inputs": inputs}
        print(f"{kind}: {covered}/{total} product lines")
    current_context, _, _ = coverage_context(ROOT, database, profile, env)
    if state != source_state(ROOT) or database_hash != digest(database) or current_context != context:
        raise ValueError("Coverage inputs changed during execution")
    receipt = output / "receipt.json"
    receipt.write_text(json.dumps({"schema": 2, "sources": state, "compile_database_sha256": database_hash,
                                   "profile": profile, "context": context, "reports": reports},
                                  indent=2) + "\n")
    print(f"SONAR_COVERAGE_RECEIPT={receipt}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, ET.ParseError, make_sonar.SonarError, subprocess.CalledProcessError) as error:
        sys.exit(f"Coverage failed: {error}")
