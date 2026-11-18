# Adaptado de AoC-Gamers/SteamWorks (scripts/tools/make_sonar.py).
# SPDX-License-Identifier: GPL-3.0-only
"""Local Cppcheck and explicit SonarScanner entrypoints, invoked through Make."""
import argparse
import base64
import json
import os
import re
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIRECTORIES = ("extension", "vsp")
SETTINGS = {"SONAR_HOST_URL", "SONAR_PROJECT_KEY", "SONAR_PROJECT_NAME",
            "SONAR_QUALITY_GATE_WAIT", "SONAR_QUALITY_GATE_TIMEOUT",
            "SONAR_COMPILE_COMMANDS", "SONAR_CPPCHECK", "SONAR_SCANNER", "SONAR_COVERAGE_RECEIPT"}


class SonarError(Exception):
    pass


def configuration(root):
    # Read values as data, never source shell commands. Explicit environment wins.
    env = os.environ.copy()
    for name, allowed in ((".env", SETTINGS), (".env.secret", {"SONAR_TOKEN"})):
        path = root / name
        if not path.is_file():
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = (part.strip() for part in line.split("=", 1))
            if name == ".env" and key == "SONAR_TOKEN":
                raise SonarError("SONAR_TOKEN debe estar en .env.secret o en el entorno.")
            if key in allowed:
                if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                    value = value[1:-1]
                env.setdefault(key, value)
    return env


def scanner_context(env):
    base = env.get("SONAR_HOST_URL", "").strip().rstrip("/")
    url = urlsplit(base)
    if (url.scheme not in {"https", "http"} or not url.hostname
            or url.username is not None or url.password is not None or url.query or url.fragment):
        raise SonarError("Configura SONAR_HOST_URL con una URL sin credenciales ni query.")
    if url.scheme == "http" and url.hostname not in {"localhost", "127.0.0.1", "::1"}:
        raise SonarError("Usa HTTPS para SonarQube; HTTP solo se admite en loopback.")
    if not env.get("SONAR_TOKEN", "").strip():
        raise SonarError("Falta SONAR_TOKEN en .env.secret o en el entorno.")
    wait = env.get("SONAR_QUALITY_GATE_WAIT", "true")
    timeout = env.get("SONAR_QUALITY_GATE_TIMEOUT", "300")
    if wait not in {"true", "false"} or not timeout.isdecimal() or int(timeout) < 1:
        raise SonarError("La espera del gate debe ser true/false y su timeout un entero positivo.")
    project = env.get("SONAR_PROJECT_KEY", "SourceTV-Support")
    name = env.get("SONAR_PROJECT_NAME", "SourceTV-Support")
    if not project.strip() or not name.strip():
        raise SonarError("SONAR_PROJECT_KEY y SONAR_PROJECT_NAME no pueden estar vacíos.")
    return base, project, name, wait, timeout


def compilation_database(root, path):
    path = Path(path)
    if not path.is_absolute():
        path = root / path
    if not path.is_file():
        raise SonarError("Falta compile_commands.json; ejecuta make build-exts-linux primero.")
    entries = json.loads(path.read_text())
    if not isinstance(entries, list):
        raise SonarError("La base de compilación debe ser una lista.")
    folders = [root / directory for directory in SOURCE_DIRECTORIES]
    selected = {}
    for entry in entries:
        directory = Path(entry["directory"])
        if not directory.is_absolute():
            raise SonarError("La base debe conservar directorios absolutos del build real.")
        source = (directory / entry["file"]).resolve()
        if not any(source.is_relative_to(folder) for folder in folders):
            continue
        args = entry.get("arguments")
        if args is None:
            args = shlex.split(entry["command"])
        if not isinstance(args, list) or not all(isinstance(v, str) for v in args) or "-m32" not in args:
            raise SonarError("El análisis requiere las órdenes reales de compilación x86 (-m32).")
        # The two Support SDKs compile the same translation units. Use the
        # actual L4D2 command for each file for a consistent analysis context.
        engine = next((arg.split("=", 1)[1] for arg in args
                       if arg.startswith("-DSE_LEFT4DEAD2=")), None)
        if engine is None or f"-DSOURCE_ENGINE={engine}" not in args:
            continue
        if source in selected:
            raise SonarError(f"Hay dos órdenes L4D2 para {source.name}.")
        selected[source] = {**entry, "file": str(source), "arguments": args}
        selected[source].pop("command", None)
    expected = {p.resolve() for folder in folders for p in folder.rglob("*")
                if p.suffix in {".c", ".cpp"}}
    if not expected or set(selected) != expected:
        raise SonarError("La base no cubre exactamente las unidades C/C++ propias compiladas para L4D2.")
    return list(selected.values())


def executable(name, root):
    if "/" in name or os.sep in name:
        name = str((root / name).resolve())
    result = shutil.which(name)
    if not result:
        raise SonarError(f"Falta {name} en PATH; instala las herramientas explícitamente.")
    return result


def compiler_query(entry):
    # Keep machine, language, feature, optimization and preprocessor flags.
    # Remove only source/output and dependency-file operations.
    command = [entry["arguments"][0]]
    args = iter(entry["arguments"][1:])
    for arg in args:
        if arg in {"-o", "-MF", "-MT", "-MQ"}:
            next(args)
        elif arg in {"-c", "-MD", "-MMD", "-MP"}:
            continue
        elif arg.startswith("-") or (Path(entry["directory"]) / arg).resolve() != Path(entry["file"]).resolve():
            command.append(arg)
    language = "c" if Path(entry["file"]).suffix == ".c" else "c++"
    return [*command, "-dM", "-E", "-v", "-x", language, "-"]


def compiler_identity(entries, root, env):
    # Compilation databases omit predefined compiler macros. AMTL checks the
    # real compiler identity/version; querying it preserves that branch instead
    # of suppressing #error or inventing a compiler version for the analyzer.
    cache = {}
    contexts = []
    names = {"__GNUC__", "__GNUC_MINOR__", "__GNUC_PATCHLEVEL__", "__GNUG__",
             "__clang__", "__clang_major__", "__clang_minor__", "__clang_patchlevel__",
             "__linux__", "__linux", "linux", "__unix__", "__unix", "unix",
             "__i386__", "__i386", "i386", "__ILP32__", "_ILP32", "__ELF__",
             "__SIZEOF_POINTER__", "__SIZEOF_LONG__", "__SIZEOF_SIZE_T__"}
    for entry in entries:
        args = entry["arguments"]
        language = "c" if Path(entry["file"]).suffix == ".c" else "c++"
        command = compiler_query(entry)
        key = (entry["directory"], *command)
        if key not in cache:
            result = subprocess.run(command, input="", cwd=entry["directory"], env={**env, "LC_ALL": "C"},
                                    capture_output=True, text=True)
            if result.returncode:
                raise SonarError("No se pudieron consultar las macros del compilador real.")
            macros = {name: value for name, value in re.findall(r"^#define (\w+) (\d+)$", result.stdout, re.M)
                      if name in names}
            if "__GNUC__" not in macros and "__clang__" not in macros:
                raise SonarError("El análisis Linux requiere identidad GCC/Clang verificable.")
            includes = []
            searching = False
            for line in result.stderr.splitlines():
                if line.strip() == "#include <...> search starts here:":
                    searching = True
                elif line.strip() == "End of search list.":
                    searching = False
                elif searching:
                    directory = (Path(entry["directory"]) / line.strip()).resolve()
                    if directory.is_dir():
                        includes.append(str(directory))
            if not includes:
                raise SonarError("No se pudieron consultar los includes implícitos del compilador.")
            predefined = dict(re.findall(r"^#define (\S+) (.*)$", result.stdout, re.M))
            cache[key] = (macros, predefined, includes)
        macros, predefined, includes = cache[key]
        if language == "c++":
            contexts.append((predefined, includes))
        for name, value in macros.items():
            if not any(a == "-D" + name or a.startswith("-D" + name + "=") or a == "-U" + name for a in args):
                args.append(f"-D{name}={value}")
    # Headers are indexed independently by CXX and have no translation-unit
    # command. Supply common C++ defaults without discarding per-file flags.
    if not contexts:
        raise SonarError("La base debe incluir unidades C++ de la extensión.")
    common = dict(contexts[0][0])
    for macros, _ in contexts[1:]:
        common = {k: v for k, v in common.items() if macros.get(k) == v}
    # The compiler has already applied ordered -D/-U flags. Replaying them as
    # an unordered set would lose redefinitions and undefine-then-define order.
    directories = []
    for entry in entries:
        args = iter(entry["arguments"])
        for arg in args:
            for option in ("-isystem", "-iquote", "-idirafter", "-I"):
                if arg == option or arg.startswith(option) and len(arg) > len(option):
                    value = next(args, "") if arg == option else arg[len(option):]
                    if value:
                        directories.append(str((Path(entry["directory"]) / value).resolve()))
                    break
    directories += [directory for _, includes in contexts for directory in includes]
    global_context = {"file": "__global__", "defines": common,
                      "includes": list(dict.fromkeys(directories))}
    return entries, global_context


def redact(text, token):
    if token:
        for secret in (token, base64.b64encode((token + ":").encode()).decode()):
            text = text.replace(secret, "[REDACTED]")
    return text


def validate_cppcheck(result, output, root, token):
    if result.returncode:
        diagnostic = output / "cppcheck.log"
        diagnostic.write_text(redact(result.stdout + result.stderr, token))
        raise SonarError(f"Cppcheck falló; diagnóstico en {diagnostic.relative_to(root)}. No se ejecutó SonarScanner.")
    try:
        xml = ET.fromstring(result.stderr)
        if xml.tag != "results" or xml.find("errors") is None:
            raise ValueError()
    except (ET.ParseError, ValueError):
        raise SonarError("Cppcheck no produjo un informe XML válido; scanner cancelado.") from None
    if any(e.get("id") in {"preprocessorErrorDirective", "syntaxError", "internalError", "cppcheckError"}
           for e in xml.findall("./errors/error")):
        diagnostic = output / "cppcheck.log"
        diagnostic.write_text(redact(result.stderr, token))
        raise SonarError(f"Análisis Cppcheck incompleto; diagnóstico en {diagnostic.relative_to(root)}. Scanner cancelado.")


def run(action, default_database, root=ROOT):
    env = configuration(root)
    context = scanner_context(env) if action == "analyze" else None
    cppcheck = executable(env.get("SONAR_CPPCHECK") or "cppcheck", root)
    scanner = executable(env.get("SONAR_SCANNER") or "sonar-scanner", root) if context else None
    source_database = (root / (env.get("SONAR_COMPILE_COMMANDS") or default_database)).resolve()
    def coverage_paths():
        if not env.get("SONAR_COVERAGE_RECEIPT"):
            return []
        from local_coverage import validate_receipt
        try:
            return validate_receipt(root, Path(env["SONAR_COVERAGE_RECEIPT"]), source_database)
        except (OSError, ValueError, KeyError, ET.ParseError, SonarError) as error:
            raise SonarError(f"Coverage receipt rejected: {error}") from None

    coverage_paths()
    entries = compilation_database(root, source_database)
    local_env = {k: v for k, v in env.items() if not k.startswith("SONAR_")}
    entries, global_context = compiler_identity(entries, root, local_env)
    # Each run gets a private output directory. A failed run cannot reuse old XML.
    parent = root / ".build/sonar"
    parent.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=parent))
    database = output / "compile_commands.json"
    database.write_text(json.dumps(entries, indent=2) + "\n")
    cxx_database = output / "cxx-compilation.json"
    cxx_database.write_text(json.dumps([global_context, *entries], indent=2) + "\n")
    report = output / "cppcheck.xml"
    result = subprocess.run([cppcheck, f"--project={database}", "--platform=unix32",
                             "--enable=warning,style,performance,portability", "--xml", "--xml-version=2"],
                            cwd=root, env=local_env, capture_output=True, text=True)
    validate_cppcheck(result, output, root, env.get("SONAR_TOKEN", ""))
    report.write_text(result.stderr)
    print(f"[sonar] Informe Cppcheck: {report.relative_to(root)}", flush=True)
    if context is None:
        return
    coverage = coverage_paths()
    base, project, name, wait, timeout = context
    # The token travels only in the scanner environment, never in argv/properties.
    result = subprocess.run([scanner, f"-Dsonar.host.url={base}", f"-Dsonar.projectKey={project}",
                             f"-Dsonar.projectName={name}",
                             "-Dsonar.sourceEncoding=UTF-8",
                             "-Dsonar.sources=" + ",".join((*SOURCE_DIRECTORIES, "scripts")),
                             "-Dsonar.exclusions=**/__pycache__/**",
                             "-Dsonar.cxx.file.suffixes=.c,.cc,.cpp,.cxx,.h,.hh,.hpp,.hxx",
                             "-Dsonar.cxx.errorRecoveryEnabled=false",
                             f"-Dsonar.cxx.jsonCompilationDatabase={cxx_database}",
                             f"-Dsonar.cxx.cppcheck.reportPaths={report}",
                             f"-Dsonar.working.directory={output / 'scanner'}",
                             f"-Dsonar.qualitygate.wait={wait}", f"-Dsonar.qualitygate.timeout={timeout}",
                             *(["-Dsonar.coverageReportPaths=" + ",".join(coverage)] if coverage else [])],
                            cwd=root, env={**env, "SONAR_HOST_URL": base}, capture_output=True, text=True)
    print(redact(result.stdout + result.stderr, env["SONAR_TOKEN"]))
    if result.returncode:
        raise SonarError("SonarScanner o el quality gate falló; consulta la salida saneada.")
    print("[sonar] SonarScanner completado" + (" y quality gate aprobado." if wait == "true" else "; gate no esperado."))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("analyze", "cppcheck"))
    parser.add_argument("--compile-commands", required=True)
    args = parser.parse_args()
    try:
        run(args.action, args.compile_commands)
    except (SonarError, OSError, ValueError, KeyError, TypeError):
        # Do not expose configuration values or subprocess output in a traceback.
        error = sys.exc_info()[1]
        print(str(error) if isinstance(error, SonarError) else "[sonar] Error local de configuración o ejecución.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
