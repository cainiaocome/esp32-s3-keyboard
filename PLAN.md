# ESP32-S3 Remote HID implementation

## Goal

Implement the ESP32-S3 Wi-Fi remote USB HID keyboard described in
`docs/ESP32_S3_REMOTE_HID_SPEC.md`, with a hardware-independent test path and
an explicit optional hardware path.

## State

- Implemented: ESP-IDF project, keyboard core, USB adapter, Wi-Fi/REST/
  WebSocket adapters, developer tooling, interactive Docker development shell,
  CI, and optional hardware API tests.
- Baseline: ESP-IDF `v6.1`; the official `tusb_hid` example uses
  `espressif/esp_tinyusb`; this project pins resolved version `2.3.0`.

## Validation

- Host `make test`: passed using the g++ fallback (core) and pytest (3
  non-hardware integration tests; 4 hardware tests deselected).
- Pinned development image: built successfully and `make test` passed using
  CMake (1 C++ test executable and 3 non-hardware integration tests).
- ESP-IDF `v6.1` target build: passed for ESP32-S3; generated image is within
  the 1 MiB factory partition.
- clang-format was applied to all C/C++ sources in the development image.
- `make up` was smoke-tested through a real PTY; it starts the Compose service,
  opens Bash in `/workspace`, and automatically adds the host `dialout` GID
  plus a detected serial device when one exists. The hardware overlay was also
  validated with `/dev/null` as a harmless device stand-in.
- Shell wrappers are executable, and `scripts/bootstrap.sh` selects the pinned
  ESP-IDF Python interpreter when present. `Dockerfile.dev` exports that
  interpreter so `make bootstrap` has `pip` inside the development container.
- Remaining validation requires a physical ESP32-S3: flash, Wi-Fi/API smoke
  test, USB enumeration, and Linux host HID event observation.
- Claude Code review follow-up complete: corrected HID report framing,
  fail-closed WebSocket authentication, combo rollback, HID-send retries,
  WebSocket frame cleanup, Wi-Fi association hardening, task-failure handling,
  configuration validation/permissions, and added API/WebSocket contract tests.

## Constraints / decisions

- `KeyboardEngine` is independent of ESP-IDF and owns all pressed-key state.
- Key presses are non-blocking: down reports immediately and `tick()` performs
  scheduled releases.
- A mutex protects engine state; the backend is called while holding only the
  short engine critical section and never during network waits.
- Credentials are supplied through ignored `sdkconfig.defaults.local`,
  generated from `.env` by `scripts/generate_sdkconfig.py`.
