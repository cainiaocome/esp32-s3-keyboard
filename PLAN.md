# ESP32-S3 Remote HID implementation

## Goal

Implement the ESP32-S3 Wi-Fi remote USB HID keyboard described in
`docs/ESP32_S3_REMOTE_HID_SPEC.md`, with a hardware-independent test path and
an explicit optional hardware path.

## State

- Implemented: ESP-IDF project, keyboard core, USB adapter, Wi-Fi/REST/
  WebSocket adapters, developer tooling, CI, and optional hardware API tests.
- Baseline: ESP-IDF `v6.1`; the official `tusb_hid` example uses
  `espressif/esp_tinyusb` `^2.0.1~1`.

## Validation

- Host `make test`: passed using the g++ fallback (core) and pytest (2
  integration tests; 1 hardware test deselected).
- Pinned development image: built successfully and `make test` passed using
  CMake (1 C++ test executable and 2 integration tests).
- ESP-IDF `v6.1` target build: passed for ESP32-S3; generated image is within
  the 1 MiB factory partition.
- clang-format was applied to all C/C++ sources in the development image.
- Remaining validation requires a physical ESP32-S3: flash, Wi-Fi/API smoke
  test, USB enumeration, and Linux host HID event observation.

## Constraints / decisions

- `KeyboardEngine` is independent of ESP-IDF and owns all pressed-key state.
- Key presses are non-blocking: down reports immediately and `tick()` performs
  scheduled releases.
- A mutex protects engine state; the backend is called while holding only the
  short engine critical section and never during network waits.
- Credentials are supplied through ignored `sdkconfig.defaults.local`,
  generated from `.env` by `scripts/generate_sdkconfig.py`.
