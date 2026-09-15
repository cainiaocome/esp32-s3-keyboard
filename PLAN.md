# ESP32-S3 Remote HID implementation

## Goal

Implement the ESP32-S3 Wi-Fi remote USB HID keyboard described in
`docs/ESP32_S3_REMOTE_HID_SPEC.md`, with a hardware-independent test path and
an explicit optional hardware path.

## State

- In progress: initial ESP-IDF project, keyboard core, USB adapter, network
  adapters, developer tooling, and tests.
- Baseline: ESP-IDF `v6.1`; the official `tusb_hid` example uses
  `espressif/esp_tinyusb` `^2.0.1~1`.

## Remaining validation

- Run host unit/integration tests and formatting/lint checks.
- Build firmware with the pinned ESP-IDF Docker image if available.
- Commit and push the completed implementation to the configured GitHub
  remote.

## Constraints / decisions

- `KeyboardEngine` is independent of ESP-IDF and owns all pressed-key state.
- Key presses are non-blocking: down reports immediately and `tick()` performs
  scheduled releases.
- A mutex protects engine state; the backend is called while holding only the
  short engine critical section and never during network waits.
- Credentials are supplied through ignored `sdkconfig.defaults.local`,
  generated from `.env` by `scripts/generate_sdkconfig.py`.
