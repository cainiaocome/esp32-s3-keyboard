SHELL := /bin/bash

IDF_PYTHON ?= python3
IDF_PY ?= idf.py
IDF_TARGET ?= esp32s3
HOST_BUILD_DIR ?= build/host
ENV_FILE ?= .env
LOCAL_SDKCONFIG ?= sdkconfig.defaults.local

ifeq ($(wildcard $(LOCAL_SDKCONFIG)),)
SDKCONFIG_DEFAULTS := sdkconfig.defaults
else
SDKCONFIG_DEFAULTS := sdkconfig.defaults;$(LOCAL_SDKCONFIG)
endif

export IDF_TARGET
export SDKCONFIG_DEFAULTS

.PHONY: help bootstrap config build clean test test-unit test-integration \
        flash monitor test-hardware format lint

help:
	@echo "ESP32-S3 Remote HID"
	@echo "  make bootstrap                         install Python test dependencies"
	@echo "  make config                            generate ignored sdkconfig defaults from .env"
	@echo "  make build                             build firmware (ESP-IDF required)"
	@echo "  make test                              run all non-hardware tests"
	@echo "  make test-unit                         run C++ keyboard-core tests"
	@echo "  make test-integration                  run Python integration tests"
	@echo "  make flash PORT=/dev/ttyACM0           flash firmware"
	@echo "  make monitor PORT=/dev/ttyACM0         monitor firmware"
	@echo "  make test-hardware PORT=/dev/ttyACM0   run optional HIL tests"
	@echo "  make format                            check C/C++ formatting"
	@echo "  make lint                              run lightweight static checks"

bootstrap:
	./scripts/bootstrap.sh

config:
	@if [[ -f "$(ENV_FILE)" ]]; then \
		$(IDF_PYTHON) scripts/generate_sdkconfig.py --env-file "$(ENV_FILE)" --output "$(LOCAL_SDKCONFIG)"; \
	else \
		echo "No $(ENV_FILE) found; using credential-free sdkconfig.defaults."; \
	fi

build: config
	@command -v $(IDF_PY) >/dev/null || { echo "idf.py not found. Run make bootstrap or use Docker; see README.md." >&2; exit 1; }
	$(IDF_PY) build

clean:
	@rm -rf build

test: test-unit test-integration

test-unit:
	cmake -S tests/unit -B $(HOST_BUILD_DIR)
	cmake --build $(HOST_BUILD_DIR)
	ctest --test-dir $(HOST_BUILD_DIR) --output-on-failure

test-integration:
	$(IDF_PYTHON) -m pytest -m "not hardware"

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
	$(IDF_PYTHON) -m py_compile scripts/generate_sdkconfig.py tests/integration/test_config.py
	cmake -S tests/unit -B $(HOST_BUILD_DIR)
	cmake --build $(HOST_BUILD_DIR)
