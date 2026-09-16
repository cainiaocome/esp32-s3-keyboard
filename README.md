# ESP32-S3 Wi-Fi Remote HID Keyboard

This firmware turns an ESP32-S3 into a USB HID keyboard controlled by an authenticated
REST or WebSocket client on the local network. The keyboard engine is hardware-independent,
uses a boot-compatible six-key report, and automatically releases held keys after inactivity.

The implementation is pinned to ESP-IDF `v6.1` and uses Espressif's
`espressif/esp_tinyusb` component. The ESP32-S3 native USB pins are D- GPIO19 and D+ GPIO20.

## Hardware and USB connector

Connect the ESP32-S3 native USB/OTG connector to the target computer—the computer that should
receive keyboard input. Boards with two connectors commonly label the native connector `USB`,
`OTG`, or `USB-OTG`; the other connector is often a USB-UART bridge used for flashing/logs.
The label is board-specific, so check the board schematic or pinout if it is unclear. A port
connected to GPIO19/GPIO20 is the native device port. Do not assume the connector nearest the
edge, the USB-C connector, or the UART-labelled port is the HID port for every board.

The firmware does not depend on a board LED or button. Flashing and serial monitoring may use
the board's USB-UART port, native USB serial/JTAG, or an external serial adapter depending on
the board layout.

## Quick start

The normal development path needs no physical board:

```bash
cp .env.example .env
# Edit .env with a random API_TOKEN and the Wi-Fi credentials.
make test
make build
```

`.env` is ignored. `make config` converts it to the ignored
`sdkconfig.defaults.local`, which is passed as an additional ESP-IDF defaults file. Credentials
are compiled into the firmware in this v1; they are never printed by the scripts or normal
logs. Do not commit `.env`, `sdkconfig`, or `sdkconfig.defaults.local`.

The API refuses to start unless `API_TOKEN` is configured. The default server is plain HTTP on
port 80 and is intended for a trusted LAN only. Anyone who can observe that LAN traffic can
capture the bearer token; do not expose the device directly to the Internet.

The station refuses open networks and requires WPA2-or-stronger association with protected
management frames. This reduces rogue-AP downgrade risk, but does not replace HTTPS on an
untrusted network.

## Docker development

The pinned development image is `espressif/idf:v6.1`:

```bash
make up
# Work inside the Bash shell. Exit it with Ctrl-D; the stack remains running.
make down

docker compose run --rm dev make test
docker compose run --rm dev make build
```

On a native Linux Docker host, `make up` automatically passes `/dev/ttyACM0` to the container
when it exists. Select another device with `make up PORT=/dev/ttyUSB0`. The target also adds
the host `dialout` group by numeric GID, which is how Unix permissions work across the container
boundary. Normal tests and builds do not require a board and work even when the device is absent.

For one-off commands, a serial/JTAG device can also be passed explicitly:

```bash
docker compose run --rm --device=/dev/ttyACM0 dev make flash PORT=/dev/ttyACM0
docker compose run --rm --device=/dev/ttyACM0 dev make monitor PORT=/dev/ttyACM0
```

Use the actual `/dev/ttyACM*` or `/dev/ttyUSB*` path. After reset, the path can change when the
board re-enumerates. On the host, add your login user to `dialout` once with
`sudo usermod -aG dialout "$USER"`, then log out and back in (or run `newgrp dialout`). Verify
with `id` and `ls -l /dev/ttyACM0`. A container does not inherit supplementary groups from the
host login session; it needs the device mapping and the matching numeric group, both handled by
`make up`.
Avoid `--privileged`; pass only the required device. Raw USB HID observation may additionally
need a narrowly scoped `/dev/bus/usb` mapping and suitable permissions.

Docker Desktop on macOS and Windows does not generally provide arbitrary USB passthrough to a
Linux container. Build and run normal tests in the container, then flash/monitor from the host,
or use a Linux machine/VM with the board attached. USB access is not required for `make test` or
`make build`.

If the native USB port is used for both flashing and HID, it may change device identity or
re-enumerate during flashing. Use the board's UART bridge for logs when available, and connect
the native port to the target computer for the final HID test.

## Commands

```text
make bootstrap                         install Python test dependencies
make config                            generate ignored sdkconfig defaults from .env
make up [PORT=/dev/ttyACM0]            start Docker dev stack and open a shell
make down                              stop the Docker dev stack
make build                             build firmware (ESP-IDF v6.1 required)
make test                              host unit and non-hardware integration tests
make test-unit                         C++ keyboard-core tests
make test-integration                  Python configuration tests
make flash PORT=/dev/ttyACM0           flash firmware
make monitor PORT=/dev/ttyACM0         monitor firmware
make test-hardware PORT=/dev/ttyACM0   optional reachable-device tests
make format                            clang-format check
make lint                              Python syntax and host compile checks
```

`make build` requires an active ESP-IDF v6.1 environment or the pinned Docker image. It does
not require a board. `make test-hardware` expects a flashed board and a reachable API URL:

```bash
REMOTE_HID_API_URL=http://192.168.1.42 \
REMOTE_HID_API_TOKEN="$API_TOKEN" \
make test-hardware PORT=/dev/ttyACM0
```

The optional hardware test checks booted API status, authenticated key down/up, and release-all.
The final USB event check should be performed with a Linux host observing the target USB HID
device (for example with `evtest` or `hidraw`) while sending the same API calls; the repository
does not require a particular Linux desktop input stack for ordinary CI.

## REST API

All API routes require:

```http
Authorization: Bearer <API_TOKEN>
```

The HTTP server accepts Authorization headers up to 512 bytes, including the `Bearer ` prefix.

Status:

```http
GET /api/v1/status
```

Keyboard actions use JSON and return `{"ok":true}` on success:

```http
POST /api/v1/key/down
POST /api/v1/key/up
POST /api/v1/key/press
POST /api/v1/key/combo
POST /api/v1/key/release-all
```

Examples:

```bash
curl -H "Authorization: Bearer $API_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"key":"LEFT_CTRL"}' \
  http://192.168.1.42/api/v1/key/down

curl -H "Authorization: Bearer $API_TOKEN" \
  -H 'Content-Type: application/json' \
  -d '{"key":"C"}' \
  http://192.168.1.42/api/v1/key/press

curl -H "Authorization: Bearer $API_TOKEN" \
  -X POST http://192.168.1.42/api/v1/key/release-all
```

`press` and `combo` are non-blocking. The down report is sent immediately and the maintenance
task sends the release after `duration_ms` (1–1000 ms). The default is 50 ms. A combo presses
keys in request order and releases its newly pressed keys together after the duration. More
than six normal keys are rejected without corrupting existing state; a rejected combo rolls
back only the keys it added. Calling `press` for a key that is already held is an intentional
idempotent no-op so a retry cannot release another client's hold.

Supported names include `A`–`Z`, `0`–`9`, `ENTER`, `ESC`, `BACKSPACE`, `TAB`, `SPACE`, punctuation,
`CAPS_LOCK`, `F1`–`F12`, print/scroll/pause, navigation keys, keypad keys, and
`LEFT_CTRL`, `LEFT_SHIFT`, `LEFT_ALT`, `LEFT_GUI`, `RIGHT_CTRL`, `RIGHT_SHIFT`, `RIGHT_ALT`,
`RIGHT_GUI`. Common aliases `CTRL`, `CONTROL`, `SHIFT`, `ALT`, `GUI`, `CMD`, `WIN`, and
`WINDOWS` map to the corresponding left modifier. Names are case-insensitive.

Errors use this shape:

```json
{"ok":false,"error":"unknown_key","message":"Request must contain a supported key"}
```

## WebSocket API

Connect to `/ws/v1/keyboard` with the same `Authorization: Bearer <API_TOKEN>` header. Send
text messages such as:

```json
{"type":"key_down","key":"A"}
{"type":"key_up","key":"A"}
{"type":"release_all"}
```

The server responds with the same JSON success/error shape. Invalid messages do not create a
second keyboard state machine. A WebSocket disconnect, including an abrupt TCP close, triggers
`release_all`, as do Wi-Fi
disconnects, HTTP server shutdown, USB attach/detach transitions, and the inactivity timeout.

## Safety and logging

`KEY_HOLD_TIMEOUT_MS` defaults to 10,000 ms. Key state is reset on boot, and `release-all` always
emits an empty HID report even when the logical state is already empty. The engine uses a mutex
for REST, WebSocket, Wi-Fi, and maintenance-task concurrency; it never waits on network I/O
while holding that state lock. Normal key values are not logged.

If an HID report cannot be sent, the logical state remains authoritative and the maintenance
task retries the complete current report until the backend accepts it. When the HID host attaches
again, the firmware emits an empty recovery report before accepting normal operation.
