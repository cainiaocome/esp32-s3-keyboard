# ESP32-S3 Remote HID implementation

## Goal

Implement the ESP32-S3 Wi-Fi remote USB HID keyboard described in
`docs/ESP32_S3_REMOTE_HID_SPEC.md`, with a hardware-independent test path and
an explicit optional hardware path.

## State

- Implemented: ESP-IDF project, keyboard core, USB adapter, Wi-Fi/REST/
  WebSocket adapters, fixed-marker LAN discovery, developer tooling, GHCR-backed
  interactive Docker development environment, CI image validation/publication,
  optional hardware API tests, and a supported Python REST/WebSocket client.
- Baseline: ESP-IDF `v6.1`; the official `tusb_hid` example uses
  `espressif/esp_tinyusb`; this project pins resolved version `2.3.0`.

## Validation

- Host `make test`: passed using the g++ fallback (core) and pytest (19
  non-hardware integration tests; 4 hardware tests deselected).
- Development image: based on ESP-IDF `v6.1`, preinstalls all project tools
  and test dependencies, and is consumed by Compose from GHCR with branch and
  short-SHA tags.
- ESP-IDF `v6.1` target build: passed for ESP32-S3; generated image is within
  the 1 MiB factory partition.
- clang-format was applied to all C/C++ sources in the development image.
- `make up` was smoke-tested through a real PTY; it starts the Compose service,
  opens Bash in `/workspace`, and automatically adds the host `dialout` GID
  plus a detected serial device when one exists. The hardware overlay was also
  validated with `/dev/null` as a harmless device stand-in.
- `scripts/test-container.sh` passed in the ESP-IDF development image,
  including tool checks, formatting, lint, host tests, integration tests, and
  the ESP32-S3 firmware build. CI is configured to repeat these checks through
  both `docker run` and Docker Compose before pushing GHCR tags.
- Wi-Fi PMF enforcement is configurable through `WIFI_PMF_REQUIRED`; the
  compatibility default is `false` for WPA2 ISP routers without PMF, while
  `true` restores strict Protected Management Frame enforcement. Configuration
  tests and an ESP32-S3 build passed for the default compatibility path.
- Shell wrappers are executable, and `scripts/bootstrap.sh` selects the pinned
  ESP-IDF Python interpreter when present. `Dockerfile.dev` exports that
  interpreter so `make bootstrap` has `pip` inside the development container.
- Remaining validation requires a physical ESP32-S3: flash, Wi-Fi/API smoke
  test, USB enumeration, and Linux host HID event observation.
- Claude Code review follow-up complete: corrected HID report framing,
  fail-closed WebSocket authentication, combo rollback, HID-send retries,
  WebSocket frame cleanup, Wi-Fi association hardening, task-failure handling,
  configuration validation/permissions, and added API/WebSocket contract tests.
- Python client follow-up complete: added installable `remote_hid_client` REST
  and async WebSocket interfaces, local HTTP/WebSocket contract tests, package
  wheel validation in CI, `docs/PYTHON_CLIENT.md`, and the corresponding
  documentation rule in `AGENTS.md`.
- Client organization follow-up complete: moved package metadata, library, and
  debug sequence helper under `client/`; updated Makefile/CI package paths and
  added `client/send_keys.py` plus the `remote-hid-send` console entry point.
- Discovery follow-up complete: `/api/v1/discovery` returns the fixed
  `esp32-s3-remote-hid-v1` marker without authentication, and bounded concurrent
  `find_device_ip(s)` helpers scan a caller-supplied LAN CIDR without exposing a
  chip/MAC-derived identifier.
- Python text-entry follow-up complete: `RemoteHIDClient.type()` maps printable
  ASCII text, including uppercase and shifted US-layout punctuation, to the
  existing REST press/combo operations with client tests and documentation.
- HID combo follow-up complete: combo state is assembled before transmission so
  each combo emits one complete report without an intermediate modifier-only
  report; unit tests cover atomic success and rollover rejection.

## Constraints / decisions

- `KeyboardEngine` is independent of ESP-IDF and owns all pressed-key state.
- Key presses are non-blocking: down reports immediately and `tick()` performs
  scheduled releases.
- A mutex protects engine state; the backend is called while holding only the
  short engine critical section and never during network waits.
- Credentials are supplied through ignored `sdkconfig.defaults.local`,
  generated from `.env` by `scripts/generate_sdkconfig.py`.
