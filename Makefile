# SPDX-License-Identifier: GPL-3.0-only
SHELL := bash
.DEFAULT_GOAL := help
PYTHON ?= python3
ROOT_DIR := $(shell $(PYTHON) -c 'import sys; from pathlib import Path; print(Path(sys.argv[1]).absolute().parent.as_posix())' '$(MAKEFILE_LIST)')
DEPS_DIR ?= $(ROOT_DIR)/.deps/exts-$(PLATFORM)
BUILD_DIR ?= $(ROOT_DIR)/.build/$(PLATFORM)-x86
SOURCEMOD_DIR ?= $(DEPS_DIR)/sourcemod-1.12
MMSOURCE_DIR ?= $(DEPS_DIR)/mmsource-1.12
HL2SDK_DIR ?= $(DEPS_DIR)/hl2sdk-l4d2
AMBUILD_DIR ?= $(DEPS_DIR)/ambuild
VENV_DIR ?= $(DEPS_DIR)/.venv
DIST_DIR ?= $(ROOT_DIR)/dist
RELEASE_CHANNEL ?= develop
SCRIPT_DIR := $(ROOT_DIR)/scripts

deps-exts-linux build-exts-linux package-exts-linux release-linux validate-artifact-linux clean-linux sonar sonar-cppcheck test-ci: PLATFORM := linux
deps-exts-windows build-exts-windows package-exts-windows release-windows validate-artifact-windows clean-windows: PLATFORM := windows

.PHONY: help deps-exts-linux deps-exts-windows build-exts-linux build-exts-windows \
        package-exts-linux package-exts-windows release-linux release-windows \
        validate-artifact-linux validate-artifact-windows clean-linux clean-windows \
        sonar sonar-cppcheck test test-ci

help:
	@printf '%s\n' \
	  'make deps-exts-<linux|windows>        Preparar dependencias fijadas' \
	  'make build-exts-<linux|windows>       Compilar x86 en el host correspondiente' \
	  'make package-exts-<linux|windows>     Preparar staging validado' \
	  'make release-<linux|windows>          Generar ZIP local (RELEASE_CHANNEL=develop|latest)' \
	  'make validate-artifact-<linux|windows> Verificar staging y ZIP contra el build' \
	  'make test                            Probar el contrato de distribución' \
	  'make test-public-api                 Comparar el contrato público congelado' \
	  'make test-native-sanitized           Ejecutar pruebas nativas con ASan y UBSan' \
	  'make test-native                     Ejecutar pruebas C++ puras (x86 por defecto)' \
	  'make test-abi                        Ejecutar el probe de ABI Linux x86' \
	  'make test-coverage                   Medir cobertura local C++ y Python' \
	  'make test-coverage-tooling           Validar localmente el recibo de cobertura' \
	  'make test-vsp                        Probar el VSP compilado con el SDK Linux x86' \
	  'make sonar-cppcheck / sonar          Analizar el build Linux localmente' \
	  'make clean-<linux|windows>            Limpiar la salida de AMBuild'

deps-exts-linux deps-exts-windows:
	$(PYTHON) "$(SCRIPT_DIR)/deps/fetch-native.py" --deps "$(DEPS_DIR)" --venv "$(VENV_DIR)"

NATIVE_INPUTS = --sourcemod "$(SOURCEMOD_DIR)" --metamod "$(MMSOURCE_DIR)" \
                --hl2sdk "$(HL2SDK_DIR)" --ambuild "$(AMBUILD_DIR)" --steamworks "$(STEAMWORKS_SDK_DIR)"

build-exts-linux build-exts-windows:
	$(PYTHON) "$(SCRIPT_DIR)/build/build-native.py" "$(PLATFORM)" \
	  --build "$(BUILD_DIR)" --venv "$(VENV_DIR)" $(NATIVE_INPUTS)

ARTIFACT_TOOL = $(PYTHON) "$(SCRIPT_DIR)/package/artifact_contract.py"
ARTIFACT_ARGS = "$(PLATFORM)" --build "$(BUILD_DIR)" --dist "$(DIST_DIR)" --channel "$(RELEASE_CHANNEL)" $(NATIVE_INPUTS)

package-exts-linux package-exts-windows:
	$(ARTIFACT_TOOL) stage $(ARTIFACT_ARGS)

release-linux release-windows:
	$(ARTIFACT_TOOL) release $(ARTIFACT_ARGS)

validate-artifact-linux validate-artifact-windows:
	$(ARTIFACT_TOOL) validate $(ARTIFACT_ARGS)

test-ci: test test-public-api

.PHONY: test-public-api
test-public-api:
	$(PYTHON) -B "$(ROOT_DIR)/tests/contract.py"

test:
	$(PYTHON) -B -m unittest discover -s "$(ROOT_DIR)/tests/package"

NATIVE_CXXFLAGS ?= -m32 -std=c++17 -Wall -Wextra
NATIVE_TESTS := frame_retention:.:test-native demo_payload:.:demo_payload transmission_policy:.:transmission_policy runtime_lifecycle:.:runtime_lifecycle
NATIVE_SANITIZER_FLAGS := -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all -fno-pie -no-pie

.PHONY: test-native-sanitized
test-native-sanitized:
	$(MAKE) test-native NATIVE_CXXFLAGS="$(NATIVE_CXXFLAGS) $(NATIVE_SANITIZER_FLAGS)"

.PHONY: test-native
test-native:
	@mkdir -p "$(ROOT_DIR)/.build"
	@set -euo pipefail; \
	  output="$$(mktemp -d "$(ROOT_DIR)/.build/native.XXXXXX")"; \
	  printf 'Output: %s\n' "$$output"; \
	  { $(CXX) --version; git rev-parse --verify HEAD 2>/dev/null || printf 'Source SHA unavailable\n'; printf 'Flags: %s\n' "$(NATIVE_CXXFLAGS)"; } | tee "$$output/toolchain.txt"; \
	  for test in $(NATIVE_TESTS); do \
	    IFS=: read -r source include binary <<< "$$test"; \
	    $(CXX) $(NATIVE_CXXFLAGS) -I"$(ROOT_DIR)/extension/$$include" \
	      "$(ROOT_DIR)/tests/native/$$source.cpp" -o "$$output/$$binary"; \
	    "$$output/$$binary" | tee -a "$$output/result.txt"; \
	  done


sonar:
	$(PYTHON) "$(SCRIPT_DIR)/tools/make_sonar.py" analyze --compile-commands "$(BUILD_DIR)/compile_commands.json"

sonar-cppcheck:
	$(PYTHON) "$(SCRIPT_DIR)/tools/make_sonar.py" cppcheck --compile-commands "$(BUILD_DIR)/compile_commands.json"

clean-linux clean-windows:
	$(PYTHON) -c 'import pathlib, shutil, sys; path = pathlib.Path(sys.argv[1]).absolute(); shutil.rmtree(path) if path.is_dir() and (path / ".ambuild2").is_dir() else None' "$(BUILD_DIR)"

STEAMWORKS_SDK_DIR ?= $(ROOT_DIR)/sdk_155

COVERAGE ?= coverage
PACKAGE_COVERAGE_DIR ?= $(ROOT_DIR)/.build/package-coverage

.PHONY: test-package-coverage
test-package-coverage:
	@mkdir -p "$(PACKAGE_COVERAGE_DIR)"
	COVERAGE_FILE="$(PACKAGE_COVERAGE_DIR)/.coverage" "$(COVERAGE)" run \
	  --branch --source="$(SCRIPT_DIR)/package" "$(ROOT_DIR)/tests/package/test_artifact_contract.py"
	COVERAGE_FILE="$(PACKAGE_COVERAGE_DIR)/.coverage" "$(COVERAGE)" xml \
	  -o "$(PACKAGE_COVERAGE_DIR)/coverage.xml"
	COVERAGE_FILE="$(PACKAGE_COVERAGE_DIR)/.coverage" "$(COVERAGE)" report

.PHONY: test-abi
test-abi: PLATFORM := linux
test-abi:
	@mkdir -p "$(ROOT_DIR)/.build"
	@set -euo pipefail; \
	  output="$$(mktemp -d "$(ROOT_DIR)/.build/abi.XXXXXX")"; \
	  sh_dir="$(MMSOURCE_DIR)/core/sourcehook"; \
	  $(CXX) -m32 -std=c++17 -fno-devirtualize -I"$(ROOT_DIR)/extension" -isystem "$$sh_dir" \
	    "$(ROOT_DIR)/tests/native/engine_binding.cpp" \
	    "$$sh_dir/sourcehook.cpp" "$$sh_dir/sourcehook_impl_chookmaninfo.cpp" \
	    "$$sh_dir/sourcehook_impl_chookidman.cpp" "$$sh_dir/sourcehook_impl_cproto.cpp" \
	    "$$sh_dir/sourcehook_impl_cvfnptr.cpp" -o "$$output/engine-binding"; \
	  "$$output/engine-binding" | tee "$$output/result.txt"

.PHONY: test-vsp
test-vsp: PLATFORM := linux
test-vsp: build-exts-linux
	@mkdir -p "$(ROOT_DIR)/.build"
	@set -euo pipefail; \
	  output="$$(mktemp -d "$(ROOT_DIR)/.build/vsp.XXXXXX")"; \
	  sdk="$(HL2SDK_DIR)"; \
	  vsp="$(BUILD_DIR)/package/addons/sourcetvsupport/bin/vsp_l4d2.so"; \
	  $(CXX) -m32 -std=c++17 -Wall -Wextra -Werror "$(ROOT_DIR)/tests/native/vsp_load.cpp" -ldl -o "$$output/vsp-load"; \
	  LD_LIBRARY_PATH="$$sdk/lib/linux" "$$output/vsp-load" "$$vsp"; \
	  $(CXX) -m32 -std=c++17 -Wall -Wextra -Werror -DPOSIX -DLINUX -D_LINUX -DGNUC -DCOMPILER_GCC \
	    -isystem "$$sdk/public" -isystem "$$sdk/public/engine" \
	    -isystem "$$sdk/public/tier0" -isystem "$$sdk/public/tier1" \
	    "$(ROOT_DIR)/tests/native/vsp_startup.cpp" \
	    "$(BUILD_DIR)/l4d2_tier1/linux-x86/libl4d2_tier1.a" \
	    -L"$$sdk/lib/linux" -lvstdlib_srv -ltier0_srv -ldl -o "$$output/vsp-startup"; \
	  LD_LIBRARY_PATH="$$sdk/lib/linux" \
	    "$$output/vsp-startup" "$$vsp" \
	    > "$$output/stdout.txt" 2> "$$output/stderr.txt" || { cat "$$output/"{stdout,stderr}.txt; exit 1; }; \
	  cat "$$output/"{stdout,stderr}.txt; \
	  test "$$(grep -Fxc '[STVS] VSP setup failed: engine factory did not provide VEngineCvar007.' "$$output/stderr.txt")" = 3; \
	  test "$$(grep -Fxc '[STVS] VSP early setup complete: 0 of 24 cvars available (24 missing). Exiting as intended.' "$$output/stdout.txt")" = 1; \
	  test "$$(grep -Fxc '[STVS] VSP early setup complete: 3 of 24 cvars available (21 missing). Exiting as intended.' "$$output/stdout.txt")" = 2

COVERAGE_PYTHON ?= $(PYTHON)
GCOV ?= gcov

.PHONY: test-coverage
test-coverage: PLATFORM := linux
.PHONY: test-coverage-tooling
test-coverage-tooling:
	$(PYTHON) -B -m unittest discover -s "$(ROOT_DIR)/tests/tools"

test-coverage: build-exts-linux
	$(PYTHON) "$(SCRIPT_DIR)/tools/local_coverage.py" --compile-commands "$(BUILD_DIR)/compile_commands.json" --coverage-python "$(COVERAGE_PYTHON)" \
	  --cxx="$(CXX)" --native-flags="$(NATIVE_CXXFLAGS)" --native-tests="$(NATIVE_TESTS)" --gcov="$(GCOV)"
