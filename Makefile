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
	  'make sonar-cppcheck / sonar          Analizar el build Linux localmente' \
	  'make clean-<linux|windows>            Limpiar la salida de AMBuild'

deps-exts-linux deps-exts-windows:
	$(PYTHON) "$(SCRIPT_DIR)/deps/fetch-native.py" --deps "$(DEPS_DIR)" --venv "$(VENV_DIR)"

build-exts-linux build-exts-windows:
	$(PYTHON) "$(SCRIPT_DIR)/build/build-native.py" "$(PLATFORM)" \
	  --build "$(BUILD_DIR)" --sourcemod "$(SOURCEMOD_DIR)" --metamod "$(MMSOURCE_DIR)" \
	  --hl2sdk "$(HL2SDK_DIR)" --venv "$(VENV_DIR)" $(BUILD_EXTRA)

ARTIFACT_TOOL = $(PYTHON) "$(SCRIPT_DIR)/package/artifact_contract.py"
ARTIFACT_ARGS = "$(PLATFORM)" --build "$(BUILD_DIR)" --dist "$(DIST_DIR)" --channel "$(RELEASE_CHANNEL)"

package-exts-linux package-exts-windows:
	$(ARTIFACT_TOOL) stage $(ARTIFACT_ARGS)

release-linux release-windows:
	$(ARTIFACT_TOOL) release $(ARTIFACT_ARGS)

validate-artifact-linux validate-artifact-windows:
	$(ARTIFACT_TOOL) validate $(ARTIFACT_ARGS)

test-ci: test

test:
	$(PYTHON) -B "$(ROOT_DIR)/tests/package/test_artifact_contract.py"

sonar:
	$(PYTHON) "$(SCRIPT_DIR)/tools/make_sonar.py" analyze --compile-commands "$(BUILD_DIR)/compile_commands.json"

sonar-cppcheck:
	$(PYTHON) "$(SCRIPT_DIR)/tools/make_sonar.py" cppcheck --compile-commands "$(BUILD_DIR)/compile_commands.json"

clean-linux clean-windows:
	$(PYTHON) -c 'import pathlib, shutil, sys; path = pathlib.Path(sys.argv[1]).absolute(); shutil.rmtree(path) if path.is_dir() and (path / ".ambuild2").is_dir() else None' "$(BUILD_DIR)"

STEAMWORKS_SDK_DIR ?= $(ROOT_DIR)/sdk_155
BUILD_EXTRA = --steamworks "$(STEAMWORKS_SDK_DIR)"

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
