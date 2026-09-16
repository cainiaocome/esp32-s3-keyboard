SHELL := /bin/bash

IDF_PYTHON ?= python3
IDF_PY ?= idf.py
IDF_TARGET ?= esp32s3
HOST_BUILD_DIR ?= build/host
ENV_FILE ?= .env
LOCAL_SDKCONFIG ?= sdkconfig.defaults.local
PORT ?= /dev/ttyACM0
REMOTE_HID_DEVICE ?= $(PORT)
DIALOUT_GID ?= $(shell getent group dialout 2>/dev/null | cut -d: -f3)
COMPOSE ?= docker compose
DEV_IMAGE ?= ghcr.io/cainiaocome/esp32-s3-keyboard-dev:master
COMPOSE_PULL ?= always

SDKCONFIG_DEFAULTS = sdkconfig.defaults$(if $(wildcard $(LOCAL_SDKCONFIG)),;$(LOCAL_SDKCONFIG))

export IDF_TARGET
export SDKCONFIG_DEFAULTS
export REMOTE_HID_DEVICE
export DIALOUT_GID
export DEV_IMAGE

.PHONY: help bootstrap config build clean test test-unit test-integration \
        test-container up down flash monitor test-hardware format lint

help:
	@echo "ESP32-S3 Remote HID"
	@echo "  make bootstrap                         compatibility no-op (Docker is pre-bootstrapped)"
	@echo "  make config                            generate ignored sdkconfig defaults from .env"
	@echo "  make up [PORT=/dev/ttyACM0]            start Docker dev stack and open a shell"
	@echo "  make down                              stop the Docker dev stack"
	@echo "  make build                             build firmware (run inside make up shell)"
	@echo "  make test                              run all non-hardware tests"
	@echo "  make test-unit                         run C++ keyboard-core tests"
	@echo "  make test-integration                  run Python integration and client tests"
	@echo "  make test-container                    validate the complete Docker dev environment"
	@echo "  make flash PORT=/dev/ttyACM0           flash firmware"
	@echo "  make monitor PORT=/dev/ttyACM0         monitor firmware"
	@echo "  make test-hardware PORT=/dev/ttyACM0   run optional HIL tests"
	@echo "  make format                            check C/C++ formatting"
	@echo "  make lint                              run lightweight static checks"

bootstrap:
	@echo "The published Docker development image is already bootstrapped; no action is required."
	@echo "Run 'make up' to enter the canonical development environment."

config:
	@if [[ -f "$(ENV_FILE)" ]]; then \
		$(IDF_PYTHON) scripts/generate_sdkconfig.py --env-file "$(ENV_FILE)" --output "$(LOCAL_SDKCONFIG)"; \
	else \
		echo "No $(ENV_FILE) found; using credential-free sdkconfig.defaults."; \
	fi

build: config
	@command -v $(IDF_PY) >/dev/null || { echo "idf.py not found. Run 'make up' and build inside the Docker shell." >&2; exit 1; }
	$(IDF_PY) build

clean:
	@rm -rf build

test: test-unit test-integration

test-unit:
	@if command -v cmake >/dev/null 2>&1; then \
		cmake -S tests/unit -B $(HOST_BUILD_DIR) && \
		cmake --build $(HOST_BUILD_DIR) && \
		ctest --test-dir $(HOST_BUILD_DIR) --output-on-failure; \
	else \
		echo "cmake not found; using the g++ host-test fallback."; \
		mkdir -p $(HOST_BUILD_DIR); \
		g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror \
			-Icomponents/keyboard_core/include \
			tests/unit/keyboard_core_tests.cpp \
			components/keyboard_core/src/key_map.cpp \
			components/keyboard_core/src/keyboard_engine.cpp \
			-o $(HOST_BUILD_DIR)/keyboard_core_tests && \
		$(HOST_BUILD_DIR)/keyboard_core_tests; \
	fi

test-integration:
	PYTHONPATH="$(CURDIR)/client$${PYTHONPATH:+:$${PYTHONPATH}}" \
		$(IDF_PYTHON) -m pytest -m "not hardware"

test-container:
	./scripts/test-container.sh

up:
	@set -e; \
	compose_files="-f compose.yaml"; \
	if [[ -e "$(REMOTE_HID_DEVICE)" ]]; then \
		compose_files="$$compose_files -f compose.hardware.yaml"; \
		echo "Passing $(REMOTE_HID_DEVICE) to the container (dialout GID: $${DIALOUT_GID:-unknown})."; \
	else \
		echo "$(REMOTE_HID_DEVICE) is not present; starting a build/test-only container."; \
	fi; \
	if [[ "$(COMPOSE_PULL)" != "never" ]]; then $(COMPOSE) $$compose_files pull; fi; \
	$(COMPOSE) $$compose_files up --pull "$(COMPOSE_PULL)" -d; \
	$(COMPOSE) $$compose_files exec dev bash -lc 'source /opt/esp/idf/export.sh && exec bash'

down:
	$(COMPOSE) down

flash: build
	@test -n "$(PORT)" || { echo "Usage: make flash PORT=/dev/ttyACM0" >&2; exit 2; }
	$(IDF_PY) -p "$(PORT)" flash

monitor:
	@test -n "$(PORT)" || { echo "Usage: make monitor PORT=/dev/ttyACM0" >&2; exit 2; }
	$(IDF_PY) -p "$(PORT)" monitor

test-hardware:
	@test -n "$(PORT)" || { echo "Usage: make test-hardware PORT=/dev/ttyACM0" >&2; exit 2; }
	REMOTE_HID_PORT="$(PORT)" $(IDF_PYTHON) -m pytest -m hardware

format:
	@command -v clang-format >/dev/null || { echo "clang-format is required for format checks." >&2; exit 1; }
	@files=$$(find components main tests/unit -type f \( -name '*.cpp' -o -name '*.hpp' \) -print); \
	clang-format --dry-run --Werror $$files

lint:
	$(IDF_PYTHON) -m compileall -q scripts tests
	$(MAKE) test-unit
