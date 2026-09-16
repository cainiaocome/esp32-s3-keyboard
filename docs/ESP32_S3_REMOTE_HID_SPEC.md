# ESP32-S3 Wi-Fi Remote HID Keyboard — Implementation Specification

## 1. Purpose

Build firmware for an ESP32-S3 that behaves as a USB HID keyboard while exposing a small Wi-Fi control API.

The device is physically connected to a target computer over the ESP32-S3 native USB/OTG interface. A client on the network sends commands to the ESP32-S3 over HTTP or WebSocket, and the ESP32-S3 emits standard USB HID keyboard reports to the target computer.

The first version should remain deliberately small, testable, and easy to extend.

Primary goals:

- ESP32-S3 acts as a normal USB HID keyboard.
- ESP32-S3 connects to a configured Wi-Fi network.
- A simple REST API controls key state.
- A WebSocket endpoint optionally supports low-latency interactive keyboard forwarding.
- Core keyboard behavior is hardware-independent and thoroughly unit-tested.
- Firmware can be built and most tests can run entirely inside a Docker container.
- Real-hardware flashing and hardware-in-the-loop tests are supported when the ESP32-S3 is made available to the development environment.
- The implementation must fail safely: network disconnects, malformed requests, client crashes, or internal errors must not leave modifier keys stuck indefinitely.

Non-goals for v1:

- Full remote desktop/KVM video.
- Internet-facing cloud service.
- Bluetooth keyboard support.
- Arbitrary scripting language on the ESP32.
- Complex user/account management.
- USB mass storage or virtual media.
- Production-grade PKI/TLS provisioning.

The project should nevertheless be structured so mouse HID, consumer/media keys, macro sequences, and composite USB devices can be added later.

---

## 2. Recommended Technology Stack

Use native ESP-IDF rather than Arduino for the primary implementation.

Recommended baseline:

- MCU: ESP32-S3
- Framework: ESP-IDF
- USB device stack: Espressif TinyUSB integration (`esp_tinyusb`)
- USB class: HID keyboard
- Network: ESP-IDF Wi-Fi station mode
- Web server: `esp_http_server`
- WebSocket: ESP-IDF HTTP server WebSocket support
- JSON: prefer `cJSON`, already commonly available in ESP-IDF
- Unit testing: host-native C/C++ tests for hardware-independent code
- Firmware/target tests: Unity and/or `pytest-embedded`
- E2E/HIL test driver: Python + pytest
- Build system: standard ESP-IDF CMake

Pin the ESP-IDF version used by CI and the development container rather than building against an unpinned `latest` image.

At the time this specification was written, ESP-IDF 6.x is current. Codex should verify the currently supported stable ESP-IDF release and the ESP32-S3 TinyUSB HID example before implementation. If there is no project-specific reason to choose otherwise, pin an exact stable release such as `v6.0.3` or a newer stable release verified to support ESP32-S3 USB HID.

Do not silently upgrade ESP-IDF major/minor versions later. Version upgrades should be explicit changes with tests.

---

## 3. High-Level Architecture

```text
                    Wi-Fi
                      |
          +-----------+-----------+
          |                       |
       REST API                 WebSocket
          |                       |
          +-----------+-----------+
                      |
                Command Layer
                      |
               Keyboard Engine
               /      |       \
          Key Map   State    Safety
                    Machine   Timeout
                      |
                HID Backend API
                  /          \
          Fake/Test HID     TinyUSB HID
                                |
                             USB OTG
                                |
                         Target Computer
```

A strict boundary must exist between the keyboard/domain logic and ESP-IDF/TinyUSB hardware APIs.

The REST/WebSocket handlers must never contain the authoritative keyboard state themselves.

---

## 4. Hardware Assumptions

The board must be an ESP32-S3 board exposing the chip's native USB/OTG device interface.

On ESP32-S3, native USB uses:

- D-: GPIO19
- D+: GPIO20

Many development boards expose this through a USB connector labeled `USB`, `OTG`, or similar. Some boards have two USB connectors: one may be a USB-to-UART bridge and the other the ESP32-S3 native USB interface.

The README must explain how to identify which connector is the native USB port for common board layouts, without assuming one exact Amazon board model.

The implementation should not hard-code board-specific LEDs, buttons, or external peripherals unless protected by configuration.

---

## 5. Repository Layout

Use a straightforward layout similar to:

```text
.
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv                  # only if custom partitioning becomes necessary
├── main/
│   ├── CMakeLists.txt
│   ├── app_main.cpp
│   ├── app_config.cpp
│   ├── app_config.hpp
│   ├── wifi_manager.cpp
│   ├── wifi_manager.hpp
│   ├── http_server.cpp
│   ├── http_server.hpp
│   ├── websocket_server.cpp
│   └── websocket_server.hpp
│
├── components/
│   ├── keyboard_core/
│   │   ├── CMakeLists.txt
│   │   ├── include/
│   │   │   ├── keyboard_engine.hpp
│   │   │   ├── hid_backend.hpp
│   │   │   ├── hid_report.hpp
│   │   │   ├── key_codes.hpp
│   │   │   ├── key_map.hpp
│   │   │   └── clock.hpp
│   │   └── src/
│   │       ├── keyboard_engine.cpp
│   │       └── key_map.cpp
│   │
│   └── usb_hid/
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── tinyusb_hid_backend.hpp
│       └── src/
│           └── tinyusb_hid_backend.cpp
│
├── tests/
│   ├── unit/
│   │   ├── CMakeLists.txt
│   │   ├── fake_hid_backend.hpp
│   │   ├── fake_clock.hpp
│   │   ├── test_key_map.cpp
│   │   ├── test_keyboard_engine.cpp
│   │   ├── test_combo.cpp
│   │   ├── test_release_all.cpp
│   │   └── test_timeout.cpp
│   │
│   ├── integration/
│   │   ├── test_api.py
│   │   └── test_websocket.py
│   │
│   └── hardware/
│       ├── conftest.py
│       ├── test_boot.py
│       ├── test_api_hardware.py
│       └── test_usb_hid_e2e.py
│
├── scripts/
│   ├── bootstrap.sh
│   ├── build.sh
│   ├── test.sh
│   ├── flash.sh
│   ├── monitor.sh
│   └── hil-test.sh
│
├── Dockerfile.dev
├── compose.yaml                    # optional development helper only
├── Makefile
├── pytest.ini
├── .env.example
├── .gitignore
└── README.md
```

The exact filenames may differ slightly, but keep the separation between pure keyboard logic and hardware/network adapters.

Do not over-engineer with dependency injection frameworks, service locators, message buses, or unnecessary abstractions.

---

## 6. Keyboard Core Design

### 6.1 HID Backend Interface

Define a tiny interface representing the ability to send the current HID keyboard report.

Conceptually:

```cpp
class HidBackend {
public:
    virtual ~HidBackend() = default;
    virtual bool send_report(const HidReport& report) = 0;
};
```

`KeyboardEngine` depends only on this interface.

Production implementation:

```text
TinyUsbHidBackend
```

Test implementation:

```text
FakeHidBackend
```

The fake backend records every report so tests can assert exact behavior.

### 6.2 Clock Interface

Time-dependent behavior must use an injectable clock or timer abstraction.

Do not use real sleeps in unit tests.

Conceptually:

```cpp
class Clock {
public:
    virtual uint64_t now_ms() const = 0;
};
```

Provide:

- production clock backed by ESP timer APIs
- fake clock for unit tests

### 6.3 Authoritative State

`KeyboardEngine` owns the complete logical pressed-key state.

It must track at least:

- currently pressed modifier keys
- currently pressed normal keys
- time of last relevant client activity
- optional ownership/session metadata if needed for WebSocket cleanup

The HID backend is an output device, not the source of truth.

### 6.4 Key Down

`key_down(key)`:

1. Validate key.
2. If already pressed, behave idempotently unless there is a specific documented reason otherwise.
3. Update state.
4. Emit the complete current HID report.
5. Return a structured result.

### 6.5 Key Up

`key_up(key)`:

1. Validate key.
2. Remove it from pressed state.
3. Releasing an already released valid key should be safe and idempotent.
4. Emit the complete current HID report if state changed, or consistently document if no-op operations do not emit.

### 6.6 Key Press

`key_press(key)` means a complete press/release operation.

It should produce:

```text
key down
short configurable delay
key up
```

The API should not block an HTTP server task for a long period.

For v1, a small default press duration is acceptable. Keep it configurable and bounded.

### 6.7 Hold and Release Semantics

A separate internal `hold` primitive is unnecessary: `key_down` without `key_up` already means held.

If the public API exposes `/hold` and `/release` aliases for ergonomics, implement them as aliases of down/up and document that clearly.

Canonical operations should remain:

- down
- up
- press
- release-all

### 6.8 Release All

`release_all()` is mandatory.

It must:

- clear all normal keys
- clear all modifiers
- emit an empty HID keyboard report
- succeed safely even if no keys are currently held

This operation is a safety mechanism and should be usable from REST, WebSocket cleanup, timeout cleanup, and internal error recovery.

### 6.9 Modifier Keys

Support at least:

- LEFT_CTRL
- LEFT_SHIFT
- LEFT_ALT
- LEFT_GUI
- RIGHT_CTRL
- RIGHT_SHIFT
- RIGHT_ALT
- RIGHT_GUI

Aliases such as `CTRL`, `SHIFT`, `ALT`, `GUI`, `CMD`, or `WIN` may be accepted, but canonical names must be documented.

Do not confuse keyboard characters with USB HID usage codes.

### 6.10 Standard Key Names

Support a practical baseline set:

- A-Z
- 0-9
- ENTER
- ESC
- BACKSPACE
- TAB
- SPACE
- MINUS
- EQUAL
- LEFT_BRACKET
- RIGHT_BRACKET
- BACKSLASH
- SEMICOLON
- APOSTROPHE
- GRAVE
- COMMA
- DOT
- SLASH
- CAPS_LOCK
- F1-F12
- PRINT_SCREEN
- SCROLL_LOCK
- PAUSE
- INSERT
- HOME
- PAGE_UP
- DELETE
- END
- PAGE_DOWN
- RIGHT
- LEFT
- DOWN
- UP

Additional keys are fine, but the mapping must be table-driven and unit-tested.

### 6.11 6-Key Rollover

For a traditional boot-keyboard-compatible report, the report may contain a modifier bitmap plus up to six normal keys.

For v1, prefer a conventional boot-compatible keyboard report for broad compatibility.

When more than the supported number of normal keys would be held simultaneously:

- do not corrupt internal state
- do not silently evict an arbitrary previously held key
- return a clear error to the caller
- keep the currently valid report intact

NKRO can be a future enhancement.

---

## 7. USB HID Implementation

Use ESP-IDF's supported TinyUSB integration for ESP32-S3.

Add the supported TinyUSB component through the ESP-IDF component manager as appropriate for the selected IDF version, for example with the equivalent of:

```bash
idf.py add-dependency esp_tinyusb
```

Codex must inspect the selected ESP-IDF version's official `tusb_hid` example rather than copying stale third-party code.

Implement a standard keyboard HID report descriptor.

For v1, optimize for compatibility with:

- Windows
- macOS
- Linux
- BIOS/UEFI where practical

If the selected TinyUSB API supports boot keyboard protocol cleanly, enable/configure it appropriately.

USB implementation details must stay inside the USB adapter component.

`KeyboardEngine` must not include TinyUSB headers.

---

## 8. Wi-Fi Configuration

Use Wi-Fi station mode in v1.

Credentials must not be committed to git.

Support build-time/deployment configuration through one straightforward mechanism, preferably environment-to-sdkconfig generation or a small ignored local config file.

At minimum configure:

```text
WIFI_SSID
WIFI_PASSWORD
API_TOKEN
```

Provide `.env.example` with placeholders only.

If practical, support ESP-IDF NVS for configuration later, but do not delay v1 for a provisioning portal.

On Wi-Fi disconnect:

- reconnect automatically
- do not reboot-loop unnecessarily
- apply the keyboard safety policy described below

---

## 9. REST API

Use JSON requests and responses.

Version the API from the beginning:

```text
/api/v1/...
```

### 9.1 Status

```http
GET /api/v1/status
```

Example response:

```json
{
  "ok": true,
  "wifi_connected": true,
  "usb_mounted": true,
  "pressed_keys": ["LEFT_CTRL", "A"],
  "uptime_ms": 123456
}
```

Do not expose Wi-Fi passwords or API tokens.

### 9.2 Key Down

```http
POST /api/v1/key/down
Authorization: Bearer <token>
Content-Type: application/json

{
  "key": "A"
}
```

### 9.3 Key Up

```http
POST /api/v1/key/up
Authorization: Bearer <token>
Content-Type: application/json

{
  "key": "A"
}
```

### 9.4 Key Press

```http
POST /api/v1/key/press
Authorization: Bearer <token>
Content-Type: application/json

{
  "key": "ENTER",
  "duration_ms": 50
}
```

`duration_ms` should be optional and restricted to a sensible range.

### 9.5 Release All

```http
POST /api/v1/key/release-all
Authorization: Bearer <token>
```

### 9.6 Combo

Optional but recommended in v1:

```http
POST /api/v1/key/combo
Authorization: Bearer <token>
Content-Type: application/json

{
  "keys": ["LEFT_CTRL", "C"],
  "duration_ms": 50
}
```

Semantics:

1. press specified keys in a deterministic order
2. wait the short configured duration
3. release all keys from this combo in reverse or well-documented order
4. preserve correctness even if an error occurs

Avoid implementing arbitrary long macro execution in the first release.

### 9.7 Text Typing

`/text` is useful but should be considered optional for v1 because text-to-keyboard conversion depends on keyboard layout.

If implemented:

```http
POST /api/v1/text
Authorization: Bearer <token>
Content-Type: application/json

{
  "text": "hello world"
}
```

Initially support a documented US-English keyboard layout only.

Do not claim Unicode support unless it is actually implemented.

### 9.8 Error Format

Use consistent JSON errors:

```json
{
  "ok": false,
  "error": "unknown_key",
  "message": "Unknown key: FOO"
}
```

Use appropriate HTTP status codes:

- 200/204 success
- 400 malformed request or invalid key
- 401 unauthorized
- 404 unknown endpoint
- 409/422 for impossible keyboard state where appropriate
- 500 only for internal failures

---

## 10. WebSocket API

Provide a WebSocket endpoint for interactive low-latency control:

```text
/ws/v1/keyboard
```

Use the same authentication token during connection setup or through an explicitly documented secure mechanism.

Messages may look like:

```json
{"type":"key_down","key":"A"}
{"type":"key_up","key":"A"}
{"type":"release_all"}
```

The WebSocket handler should translate messages into the same `KeyboardEngine` calls used by REST.

Do not create a separate keyboard implementation for WebSocket.

On WebSocket disconnect, default to `release_all()` unless a future explicit multi-client ownership model changes this behavior.

For v1, keeping WebSocket control effectively single-owner is acceptable and simpler.

---

## 11. Authentication and Network Safety

This project controls a physical keyboard and therefore should not expose an unauthenticated API by default.

Minimum v1 protection:

- random API token
- `Authorization: Bearer <token>` for REST
- authenticated WebSocket connection
- bind only to the ESP32's local network interface
- no cloud exposure by default

The token must not be hard-coded in source control.

Use constant-time comparison if convenient, but do not over-engineer a complete identity system.

TLS is optional for a trusted LAN v1. The README must make clear that plain HTTP exposes the token to anyone capable of observing that LAN traffic.

A future version may add HTTPS or local-device provisioning.

---

## 12. Safety / Stuck-Key Protection

Stuck keys are one of the most important failure modes.

Implement all of the following:

1. A public `release-all` operation.
2. Automatic release on controlling WebSocket disconnect.
3. An inactivity safety timeout for held keys.
4. Release-all during relevant network/session failures.
5. Release-all when the HID layer is reinitialized.
6. State reset on boot.

Suggested default:

```text
MAX_HOLD_TIME_MS=10000
```

The exact default can be adjusted, but it must be configurable.

When one or more keys are held and no valid controlling activity refreshes the safety timer before expiry:

```text
KeyboardEngine -> release_all()
```

Use an injectable clock so this behavior can be tested instantly.

Never implement unit tests using a real 10-second sleep.

---

## 13. Concurrency

REST requests, WebSocket messages, timeout callbacks, and USB events may run in different ESP-IDF task contexts.

`KeyboardEngine` must have a clear synchronization model.

Use a simple mutex/critical section or serialize commands through one small queue/task.

Prefer the simplest reliable option.

Requirements:

- no simultaneous mutation of keyboard state
- no deadlocks on release-all
- no long network wait while holding keyboard-state locks
- no unbounded queues

Do not introduce a large event-bus architecture.

---

## 14. Unit Testing Strategy

Most functionality must be testable without an ESP32 attached.

### 14.1 Fake HID Backend

Create a fake backend that records reports:

```text
reports = [
  report_1,
  report_2,
  ...
]
```

Tests assert exact report sequences.

### 14.2 Fake Clock

Create a controllable clock:

```text
clock.advance_ms(10001)
engine.tick()
```

Then assert that an empty HID report was emitted.

### 14.3 Required Keyboard Tests

Cover at least:

- key mapping for representative letters, digits, special keys, function keys, and modifiers
- `down(A)` emits A pressed
- `up(A)` removes A
- `press(A)` emits down then up
- repeated `down(A)` is safe
- repeated `up(A)` is safe
- Ctrl+C sequence is correct
- multiple modifiers work
- release-all clears everything
- timeout releases held keys
- invalid key does not change state
- rollover limit is enforced safely
- HID backend failure leaves a well-defined state
- state survives no-op commands correctly

### 14.4 REST Handler Tests

Test handlers with fake keyboard backend where practical.

Required cases:

- valid key-down
- valid key-up
- valid key-press
- release-all
- combo
- missing body
- invalid JSON
- unknown key
- unauthorized request
- wrong token
- too-long request body
- out-of-range duration

### 14.5 WebSocket Tests

Cover:

- authentication
- valid down/up messages
- invalid JSON
- unknown command
- disconnect triggers release-all
- repeated connect/disconnect does not leak resources

---

## 15. Firmware / ESP32 Target Tests

Use ESP-IDF's normal target testing capabilities where they provide value.

Recommended tools:

- Unity for tests executed on the ESP32
- `pytest`
- `pytest-embedded`
- relevant ESP-IDF pytest services

Hardware tests should verify at least:

- firmware boots
- Wi-Fi initializes
- API server starts
- status endpoint responds
- USB HID stack initializes
- no boot loop occurs
- release-all remains functional

Keep target tests smaller than host-native tests. Business logic should remain primarily covered by the faster host test suite.

---

## 16. Full Hardware-in-the-Loop USB Test

Design for an optional true E2E test:

```text
pytest runner
     |
     | Wi-Fi / HTTP
     v
ESP32-S3
     |
     | USB HID
     v
Linux USB host
```

Example test flow:

1. ESP32 firmware is running.
2. Test runner sends `key_down(A)` over HTTP.
3. Linux host observes the actual USB HID keyboard report/event.
4. Test asserts A is down.
5. Test runner sends `key_up(A)`.
6. Linux host observes release.
7. Test asserts no key remains held.

Possible host-side tools include Linux evdev/hidraw APIs or a small HID test program. Codex should choose a reliable method based on the actual test host and document it.

This HIL suite must be optional and must not block ordinary development in environments where the board is unavailable.

Mark tests appropriately, for example:

```bash
pytest -m hardware
```

Normal CI should be able to run:

```bash
pytest -m "not hardware"
```

---

## 17. Docker / Codex Development Environment

Codex is expected to run inside a Docker container.

This is the canonical development workflow. The repository's published GHCR
development image is used by Docker Compose; users should not need to build a
local image or bootstrap dependencies after entering the container.

### 17.1 What Works Normally in Docker

The following should work without physical USB passthrough:

- edit source code
- install dependencies
- compile firmware
- run static analysis
- run formatting/linting
- run all host-native keyboard tests
- run parser/API logic tests that do not require a physical ESP32
- build release artifacts
- inspect generated binaries and sizes

The project image is published as
`ghcr.io/cainiaocome/esp32-s3-keyboard-dev`. Its `master` tag tracks the
default branch, and `sha-<short-commit>` tags provide exact selection by
commit. The image is built from a pinned official `espressif/idf` base and
contains ESP-IDF, CMake, Ninja, toolchains, Python, clang-format, and the
project's pinned test dependencies.

### 17.2 Preferred Container Setup

If practical, base the project's development image on a pinned official ESP-IDF image:

```dockerfile
FROM espressif/idf:v6.1
```

The exact tag should match the version selected for this repository.

Install only additional project tools on top, for example:

- git
- curl
- jq
- Python test dependencies
- clang-format if not already available
- cppcheck or clang-tidy only if actually used

Do not duplicate the ESP toolchain unnecessarily.

The image must be fully usable immediately after `make up`; a bootstrap step
must not be required for build, test, format, lint, flash, or monitor commands.

### 17.3 Accessing the Real ESP32 From Docker on Linux

On a Linux Docker host, a USB serial/JTAG device can usually be exposed to the Codex container using Docker device passthrough.

Typical examples may look like:

```bash
docker run --device=/dev/ttyACM0 ...
```

or:

```bash
docker run --device=/dev/ttyUSB0 ...
```

The exact device depends on the board and USB mode.

For workflows that need access to raw USB endpoints, a controlled `/dev/bus/usb` passthrough or appropriate specific device mapping may be needed.

Avoid `--privileged` by default. Grant only the required device access.

The container user must also have sufficient permissions for the device node.

Document troubleshooting for:

- permission denied
- device changes after reset/re-enumeration
- serial port path changes
- USB native port vs USB-UART bridge confusion

### 17.4 Docker Desktop on macOS / Windows

Do not assume that arbitrary USB device passthrough into a normal Docker Desktop Linux container works like it does on a native Linux Docker host.

If Codex runs in Docker Desktop and cannot access the physical ESP32 directly, that is not a blocker.

Use one of these workflows:

1. Codex builds/tests in its container; flash/monitor is run from the host.
2. Codex generates the firmware binary and a host-side script flashes it.
3. Use a separate Linux machine or VM that has the ESP32 physically attached for HIL tests.
4. Use a CI/self-hosted Linux runner with the board attached if automated HIL is desired later.

The repository must therefore not require USB access just to build or run normal tests.

### 17.5 Required Separation

The project should support, from the shell opened by `make up`:

```bash
make build
make test
make format
make lint
make test-container
```

without hardware.

Physical operations should be explicit:

```bash
make flash PORT=/dev/ttyACM0
make monitor PORT=/dev/ttyACM0
make test-hardware PORT=/dev/ttyACM0
```

If no port is supplied, scripts may attempt detection but must print what they selected and allow override.

---

## 18. Development Dependencies

Codex should be allowed to install project dependencies itself.

Prefer scripted/reproducible installation over undocumented manual setup.

### 18.1 Required Build Dependencies

If using the official ESP-IDF container, most of these are already present:

- Git
- Python 3
- ESP-IDF pinned version
- ESP-IDF Xtensa toolchain for ESP32-S3
- CMake
- Ninja
- ESP-IDF Python environment
- esptool

### 18.2 Firmware Dependencies

Use ESP-IDF component management for firmware dependencies.

Expected dependency:

- `esp_tinyusb`

Avoid unnecessary third-party web frameworks or JSON libraries when ESP-IDF already provides suitable functionality.

### 18.3 Test Dependencies

Use a pinned requirements file where Python packages are needed, e.g.:

```text
tests/requirements.txt
```

Potential packages:

```text
pytest
pytest-embedded
requests
websockets
```

Only include packages actually used.

Depending on the final HIL implementation, hardware tests may additionally use packages such as:

```text
pyserial
hidapi
python-evdev
```

Do not add these until the test implementation needs them.

### 18.4 Published Development Image

The development image is built and bootstrapped in CI. It must install and
configure all dependencies needed by the commands above, including the
ESP-IDF Python environment and pinned test requirements. Compose must consume
the published image rather than building locally.

An optional compatibility script may remain for host-side workflows:

```bash
./scripts/bootstrap.sh
```

If retained, it should:

1. detect whether a usable ESP-IDF environment already exists
2. install/configure the pinned version if missing, when feasible
3. install project Python test dependencies
4. initialize ESP-IDF-managed components
5. print detected versions
6. fail with actionable errors

It should be safe to run more than once.

Do not make the bootstrap script silently replace a newer/different global ESP-IDF installation on the developer's host.

It is not part of the canonical container workflow and must not be required
for normal development.

---

## 19. Makefile / Developer Commands

Provide simple top-level commands.

Minimum:

```text
make up
make down
make build
make clean
make test
make test-unit
make test-integration
make test-container
make flash PORT=/dev/ttyACM0
make monitor PORT=/dev/ttyACM0
make test-hardware PORT=/dev/ttyACM0
make format
make lint
```

`make test` must not require hardware. `make up` must pull the configured
published image, activate ESP-IDF in its interactive shell, and mount the
repository at `/workspace`.

`make build && make test` should be the standard Codex validation loop.

---

## 20. Configuration

Provide `.env.example`, but do not commit secrets.

Example:

```dotenv
WIFI_SSID=example
WIFI_PASSWORD=change-me
API_TOKEN=replace-with-random-token
KEY_HOLD_TIMEOUT_MS=10000
KEY_PRESS_DURATION_MS=50
```

The exact mechanism that transfers these values into firmware should be documented and reproducible.

Do not accidentally print passwords or tokens in normal logs.

If credentials are compiled into firmware in v1, explicitly document that limitation.

---

## 21. Logging

Use ESP-IDF logging macros.

Log important lifecycle events:

- boot
- firmware version
- Wi-Fi connect/disconnect
- IP assigned
- web server started
- USB mounted/unmounted
- safety timeout triggered
- release-all triggered by disconnect/error

Do not log every normal key press at INFO level by default, because that can expose sensitive typed data and creates unnecessary noise.

Debug-level key event logging may exist but should be off by default.

Never log API tokens or Wi-Fi passwords.

---

## 22. Web UI (Optional v1, Good Follow-Up)

A minimal embedded webpage is useful but not required for the first firmware milestone.

If implemented, it should:

- connect to the ESP32 WebSocket endpoint
- capture browser keydown/keyup events only while explicitly focused/armed
- show connection state
- expose a prominent `Release All` button
- send release-all when the page disconnects where possible
- avoid browser key-repeat causing duplicated logical state
- allow a simple on-screen modifier status display

Do not spend significant effort on UI styling.

A later version can add a mobile touchpad when mouse HID is implemented.

---

## 23. Security Boundaries

Assume the device is for the owner's own systems and trusted lab/LAN environments.

The implementation should not include features intended to hide its presence, bypass authorization, or covertly capture input.

The ESP32 is an input controller, not a keylogger.

Do not implement keyboard input capture from the target host.

Do not expose the service to the public Internet by default.

---

## 24. Future Extensions

Design interfaces so these can be added without rewriting the core:

### 24.1 Mouse HID

Potential API:

```text
POST /api/v1/mouse/move
POST /api/v1/mouse/down
POST /api/v1/mouse/up
POST /api/v1/mouse/click
POST /api/v1/mouse/scroll
```

### 24.2 Consumer / Media Keys

Examples:

- volume up/down
- mute
- play/pause
- next/previous

### 24.3 Composite USB Device

Potential future USB functions:

- keyboard
- mouse
- consumer control
- CDC serial for diagnostics

Do not add mass-storage/virtual-media support to v1.

### 24.4 Macro Engine

A future bounded macro format might support:

```text
press
key_down
key_up
wait_ms
text
combo
release_all
```

If implemented later, add strict limits for operation count and delays so a malformed macro cannot occupy the device indefinitely.

### 24.5 mDNS

Optional hostname:

```text
esp32-keyboard.local
```

### 24.6 OTA Update

ESP-IDF OTA can be a later feature after the basic firmware is stable and tested.

---

## 25. Implementation Milestones

Codex should implement in small validated steps.

### Milestone 1 — Repository and Build

- ESP-IDF project boots on ESP32-S3
- pinned published development image
- Docker development workflow
- `make build`
- CI build, container tests, Compose tests, and GHCR publication

### Milestone 2 — Hardware-Independent Keyboard Core

- HID report types
- key map
- keyboard state machine
- fake HID backend
- fake clock
- comprehensive host-native unit tests

No Wi-Fi required yet.

### Milestone 3 — USB HID

- TinyUSB HID adapter
- ESP32-S3 enumerates as keyboard
- manual smoke test
- release-all verified

### Milestone 4 — Wi-Fi + REST API

- Wi-Fi station mode
- token authentication
- status
- down/up/press/release-all
- API tests

### Milestone 5 — Safety

- hold timeout
- network/disconnect cleanup
- concurrency review
- failure-path tests

### Milestone 6 — WebSocket

- interactive key down/up
- authenticated connection
- disconnect release-all
- integration tests

### Milestone 7 — Hardware Tests

- pytest-embedded boot/API smoke tests
- optional real USB HID E2E test
- documentation for Docker device passthrough

### Milestone 8 — Optional Convenience UI

- embedded web page
- keyboard event forwarding
- release-all button

---

## 26. CI Requirements

Normal CI must not require physical hardware.

At minimum run:

```text
format check
host unit tests
non-hardware integration tests
ESP-IDF firmware build
```

For every push, CI must also build the development image, run its complete
container test suite, validate the Docker Compose workflow, and only then
publish branch-name and short-commit tags to GHCR. Pull requests run the
tests but do not publish images.

A separate optional self-hosted workflow may run HIL tests if a physical ESP32-S3 is attached.

Do not make GitHub-hosted CI depend on a real ESP32.

Store no Wi-Fi or API secrets in public CI configuration.

---

## 27. Definition of Done for v1

The first release is complete when all of the following are true:

- Firmware builds reproducibly in the documented Docker/Codex environment.
- `make up` pulls a published GHCR image and opens a ready-to-use ESP-IDF shell.
- No bootstrap command is required for any normal development command.
- CI validates the image and Compose workflow before publishing branch and
  short-commit tags.
- ESP32-S3 enumerates as a USB HID keyboard on a normal host.
- ESP32 joins configured Wi-Fi.
- Authenticated status endpoint works.
- Authenticated key down works.
- Authenticated key up works.
- Authenticated key press works.
- Authenticated release-all works.
- Ctrl/Shift/Alt/GUI modifiers work.
- Combo endpoint works if included in the v1 scope.
- Safety timeout automatically releases held keys.
- WebSocket disconnect releases held keys if WebSocket is implemented.
- Core logic unit tests run without hardware.
- `make test` does not require an ESP32.
- `make build` works in the development container.
- Hardware flash/monitor instructions are documented.
- Linux Docker USB passthrough workflow is documented.
- Docker Desktop limitation/workaround is documented.
- No credentials are committed.
- README explains which physical USB connector must be connected to the target computer.

---

## 28. Guidance for Codex

When implementing this repository:

1. Prefer official ESP-IDF APIs and official Espressif examples/documentation.
2. Inspect the selected ESP-IDF release's current TinyUSB HID example before writing the USB layer.
3. Keep `keyboard_core` independent of ESP-IDF wherever reasonably practical.
4. Write tests with each core behavior, not after the entire firmware is finished.
5. Do not require real hardware for normal tests.
6. Do not use real sleeps in unit tests; use an injectable clock.
7. Keep REST and WebSocket as thin adapters over the same keyboard engine.
8. Keep secrets outside git.
9. Run build and tests after every meaningful change.
10. If the physical board is not available inside the Codex container, finish everything that can be validated without it and provide exact host/HIL commands for the remaining validation.
11. Do not use `--privileged` Docker containers merely to make flashing easier; prefer specific device passthrough on Linux.
12. Do not redesign this into a large IoT/cloud platform. Keep the implementation small.
13. Document any deviation from this specification in the README or a short design note.

---

## 29. Useful Upstream References

Codex should verify these against the selected ESP-IDF release rather than blindly copying snippets:

- ESP32-S3 USB Device Stack / TinyUSB documentation:
  https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html

- ESP-IDF source and USB device examples:
  https://github.com/espressif/esp-idf

- ESP-IDF testing with pytest / pytest-embedded:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/esp-idf-tests-with-pytest.html

- ESP-IDF Docker image documentation:
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/tools/idf-docker-image.html

- Docker `--device` documentation:
  https://docs.docker.com/reference/cli/docker/container/run/#add-host-device-to-container---device

---

## 30. Short Summary

The core architectural rule for this project is:

```text
Network protocol != keyboard logic != USB hardware
```

Keep these as three separate layers.

That lets Codex develop and test nearly the entire project inside a Docker container, while the real ESP32-S3 is only required for flashing, USB enumeration validation, and final hardware-in-the-loop tests.
