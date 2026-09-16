# Python Client

`remote_hid_client` is the supported Python client for the ESP32-S3 Remote HID
Keyboard. It provides:

- synchronous REST methods for status and keyboard actions;
- an asynchronous, authenticated WebSocket session for interactive key state;
- consistent exceptions for API errors, connection failures, and malformed
  device responses;
- client-side validation for the firmware's duration and combo limits.

The client controls a physical keyboard. Treat the API token as a secret and
use the client only on a trusted network unless the device is placed behind a
secure transport or proxy.

## Installation

The client requires Python 3.10 or newer. From a checkout of this repository:

```bash
python -m venv .venv
. .venv/bin/activate
python -m pip install ./client
```

For a local editable install while developing the client:

```bash
python -m pip install -e ./client
```

The REST client uses Python's standard-library HTTP implementation. The
WebSocket client depends on `websockets` 15.x, which is installed by the
package. The repository's Docker development image already contains this
dependency.

The installable package, its `pyproject.toml`, and the debug helper are kept
together under `client/`; the repository root does not contain Python package
metadata.

## Device configuration

The firmware must be flashed with a non-empty `API_TOKEN`. The URL passed to
the client is the device's HTTP origin, for example:

```python
from remote_hid_client import RemoteHIDClient

client = RemoteHIDClient("http://192.168.1.42", "the-configured-api-token")
```

`http://` and `https://` are accepted. A path prefix is preserved, so
`https://keyboard.example/esp32` targets `/esp32/api/v1/...` and
`/esp32/ws/v1/keyboard`. Query strings and URL fragments are rejected. The
default operation timeout is five seconds; change it with `timeout=`:

```python
client = RemoteHIDClient(
    "http://192.168.1.42",
    "the-configured-api-token",
    timeout=10,
)
```

Every request sends `Authorization: Bearer <token>`. The token is never
written to logs by this package. Plain HTTP exposes the token to anyone able
to observe the LAN, so do not expose the ESP32 directly to the Internet.

## REST usage

REST methods are blocking and return after the ESP32 has accepted the command.
`press` and `combo` are non-blocking on the device: the down report is sent
immediately and the firmware schedules the release.

```python
from remote_hid_client import RemoteHIDClient

client = RemoteHIDClient("http://192.168.1.42", "the-configured-api-token")

status = client.status()
print(status.wifi_connected, status.usb_mounted)
print(status.pressed_keys, status.uptime_ms)

client.key_down("LEFT_CTRL")
client.key_up("LEFT_CTRL")
client.press("ENTER", duration_ms=75)
client.combo(["LEFT_CTRL", "C"], duration_ms=50)
client.release_all()
```

### REST methods

| Python method | Firmware endpoint | Behavior |
| --- | --- | --- |
| `status()` | `GET /api/v1/status` | Returns a `StatusSnapshot`. |
| `key_down(key)` | `POST /api/v1/key/down` | Holds one key. |
| `key_up(key)` | `POST /api/v1/key/up` | Releases one key. |
| `press(key, duration_ms=None)` | `POST /api/v1/key/press` | Presses one key and schedules release. |
| `combo(keys, duration_ms=None)` | `POST /api/v1/key/combo` | Presses 1–6 keys and schedules release. |
| `release_all()` | `POST /api/v1/key/release-all` | Releases all keys as a safety operation. |

`duration_ms` is omitted when it is `None`, allowing the firmware default of
50 ms. When provided, it must be an integer from 1 through 1000. A combo must
contain 1 through 6 non-empty key names. The client does not maintain keyboard
state locally; the device remains authoritative.

The firmware accepts canonical names such as `A`, `ENTER`, `F1`,
`LEFT_CTRL`, and `KEYPAD_1`, is case-insensitive, and supports common aliases
such as `CTRL`, `SHIFT`, `ALT`, `CMD`, `WIN`, and `WINDOWS`. See the [REST API
section in the README](../README.md#rest-api) for the complete key-family
summary.

## WebSocket usage

Use WebSocket when an application needs separate key-down and key-up events
with lower per-command overhead. The session is asynchronous and must be
closed when finished. Closing the connection causes the firmware to release
all held keys.

```python
import asyncio

from remote_hid_client import RemoteHIDClient


async def main() -> None:
    client = RemoteHIDClient("http://192.168.1.42", "the-configured-api-token")
    async with client.websocket() as keyboard:
        await keyboard.key_down("LEFT_SHIFT")
        await keyboard.key_down("A")
        await keyboard.key_up("A")
        await keyboard.key_up("LEFT_SHIFT")
        await keyboard.release_all()


asyncio.run(main())
```

The WebSocket client derives `ws://` from an `http://` base URL and `wss://`
from an `https://` base URL. It connects to `/ws/v1/keyboard` and sends the
same bearer token in the opening handshake. The convenience methods are:

| Python method | Message |
| --- | --- |
| `await key_down(key)` | `{"type":"key_down","key":"A"}` |
| `await key_up(key)` | `{"type":"key_up","key":"A"}` |
| `await release_all()` | `{"type":"release_all"}` |
| `await send(mapping)` | Sends a raw JSON object for forward compatibility. |

The current firmware does not define WebSocket `press` or `combo` commands;
use REST for those operations. `send()` is intentionally an escape hatch for
new firmware commands and still requires a successful `{"ok":true}` response.

## Errors

Import the exception classes when an application needs to distinguish failure
modes:

```python
from remote_hid_client import (
    RemoteHIDClient,
    RemoteHIDConnectionError,
    RemoteHIDError,
    RemoteHIDProtocolError,
)

client = RemoteHIDClient("http://192.168.1.42", "the-configured-api-token")
try:
    client.press("NOT_A_KEY")
except RemoteHIDConnectionError:
    print("device is unreachable")
except RemoteHIDError as exc:
    print(exc.error, exc.status_code, exc.message)
```

`RemoteHIDConnectionError` is a subclass of `RemoteHIDError` and
`ConnectionError`; catch it before the general API exception when the
distinction matters. `RemoteHIDProtocolError` means the response was not valid
according to the documented JSON contract.

For an HTTP rejection, `RemoteHIDError` exposes:

- `status_code`: the HTTP status, such as 400 or 401;
- `error`: the firmware's stable error code, such as `unknown_key`;
- `message`: the human-readable error message;
- `response`: the decoded error object, when the server returned JSON.

Typical errors include `unauthorized` (401), `unknown_key` (400),
`invalid_duration` (400), `invalid_combo` (400), `rollover_limit` (422), and
`hid_backend_failure` (503). The same `error` and `message` fields are
available for rejected WebSocket commands, although WebSocket command errors
do not have an HTTP status code.

Input-shape errors detected before contacting the device raise `ValueError`:
empty key names, invalid durations, and combos outside the 1–6 key limit.

## Safety and operational guidance

- Always release a key you explicitly hold, preferably in a `finally` block.
- Prefer `async with client.websocket()` so disconnect cleanup happens on
  normal exit and exceptions.
- Keep `release_all()` available as an emergency recovery action.
- The firmware also releases keys when a WebSocket disconnects, Wi-Fi drops,
  the USB HID host changes state, or the inactivity safety timeout expires.
- Do not use the client as a keylogger or send secrets through an untrusted
  network. It controls the target computer's keyboard and can cause destructive
  input.

## Testing an application

The repository's client contract tests run without hardware:

```bash
make test-integration
```

They use local HTTP and WebSocket test servers and do not require the device
token. To exercise the real firmware as well, set the hardware test variables
and run the optional hardware tests:

```bash
REMOTE_HID_API_URL=http://192.168.1.42 \
REMOTE_HID_API_TOKEN="$API_TOKEN" \
make test-hardware PORT=/dev/ttyACM0
```

Hardware tests should be run only when the ESP32 is connected and the target
USB host is ready to receive keyboard input.

## Debug sequence helper

The repository includes a small helper that sends complete `press` commands
for positional key arguments in the order given. It is useful for quickly
checking the network/API path without writing a Python program. The token is
read from `REMOTE_HID_API_TOKEN` first, then `API_TOKEN`, or can be supplied
with `--token`.

From the repository root, without installing the package:

```bash
REMOTE_HID_API_TOKEN="$API_TOKEN" \
python client/send_keys.py \
  --ip 192.168.1.42 \
  ENTER A B
```

After installing the client, the equivalent console command is:

```bash
REMOTE_HID_API_TOKEN="$API_TOKEN" \
remote-hid-send --ip 192.168.1.42 ENTER A B
```

Useful options:

- `--duration-ms 75` overrides the device's default press duration;
- `--delay-ms 100` waits between each press command;
- `--timeout 10` changes the per-request timeout;
- `--token VALUE` supplies the token directly, though an environment variable
  avoids putting it in shell history.

Example with an explicit interval:

```bash
python client/send_keys.py \
  --ip 192.168.1.42 \
  --duration-ms 75 \
  --delay-ms 100 \
  F1 F2 F3
```

The helper reports progress but never prints the token. It returns exit code 1
for invalid local input, an API rejection, or an unreachable device. It uses
REST `press`, so WebSocket-only interactive key-down/up behavior should use
the Python API shown above.
