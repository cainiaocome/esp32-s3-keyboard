"""Synchronous REST and asynchronous WebSocket clients for Remote HID.

The REST implementation deliberately uses :mod:`urllib` so the client has no
HTTP dependency. WebSocket support uses the project's pinned ``websockets``
package and is imported only when a WebSocket connection is opened.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Mapping, Sequence
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import Request, urlopen


_MAX_DURATION_MS = 1000
_USER_AGENT = "esp32-s3-remote-hid-client/0.1"


class RemoteHIDError(RuntimeError):
    """An API request was rejected or returned an invalid API response.

    Attributes:
        message: Human-readable server or client protocol message.
        status_code: HTTP status code when one was available.
        error: Stable server error code, such as ``unknown_key``.
        response: Decoded response body, when it was a JSON object.
    """

    def __init__(
        self,
        message: str,
        *,
        status_code: int | None = None,
        error: str | None = None,
        response: Mapping[str, Any] | None = None,
    ) -> None:
        super().__init__(message)
        self.message = message
        self.status_code = status_code
        self.error = error
        self.response = response


class RemoteHIDConnectionError(RemoteHIDError, ConnectionError):
    """The device could not be reached or a WebSocket could not be opened."""


class RemoteHIDProtocolError(RemoteHIDError):
    """The device returned a response that does not match the API contract."""


@dataclass(frozen=True)
class StatusSnapshot:
    """Device status returned by ``GET /api/v1/status``."""

    wifi_connected: bool
    usb_mounted: bool
    pressed_keys: tuple[str, ...]
    uptime_ms: int

    @classmethod
    def from_response(cls, response: Mapping[str, Any]) -> "StatusSnapshot":
        """Create a snapshot after validating the firmware response shape."""

        wifi_connected = response.get("wifi_connected")
        usb_mounted = response.get("usb_mounted")
        pressed_keys = response.get("pressed_keys")
        uptime_ms = response.get("uptime_ms")
        if (
            type(wifi_connected) is not bool
            or type(usb_mounted) is not bool
            or not isinstance(pressed_keys, list)
            or any(not isinstance(key, str) for key in pressed_keys)
            or type(uptime_ms) is not int
            or uptime_ms < 0
        ):
            raise RemoteHIDProtocolError(
                "Status response has an invalid shape", response=response
            )
        return cls(
            wifi_connected=wifi_connected,
            usb_mounted=usb_mounted,
            pressed_keys=tuple(pressed_keys),
            uptime_ms=uptime_ms,
        )


def _validate_duration(duration_ms: int | None) -> None:
    if duration_ms is not None and (
        type(duration_ms) is not int or not 1 <= duration_ms <= _MAX_DURATION_MS
    ):
        raise ValueError("duration_ms must be an integer from 1 to 1000")


def _validate_key(key: str) -> None:
    if not isinstance(key, str) or not key:
        raise ValueError("key must be a non-empty string")


def _validate_combo(keys: Sequence[str]) -> list[str]:
    if isinstance(keys, (str, bytes)):
        raise ValueError("keys must be a sequence of 1 to 6 key names")
    values = list(keys)
    if not 1 <= len(values) <= 6 or any(
        not isinstance(key, str) or not key for key in values
    ):
        raise ValueError("keys must be a sequence of 1 to 6 non-empty key names")
    return values


def _decode_json(raw_body: bytes, *, context: str) -> Mapping[str, Any]:
    try:
        decoded = json.loads(raw_body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise RemoteHIDProtocolError(f"{context} was not valid JSON") from exc
    if not isinstance(decoded, dict):
        raise RemoteHIDProtocolError(f"{context} must be a JSON object")
    return decoded


class RemoteHIDClient:
    """Control an ESP32-S3 Remote HID device.

    Args:
        base_url: Device HTTP origin, for example ``http://192.168.1.42``.
            A path prefix is preserved when present.
        token: API token configured in the firmware.
        timeout: Per-operation HTTP/WebSocket timeout in seconds.

    REST methods are synchronous. Use :meth:`websocket` as an async context
    manager for low-latency key state control.
    """

    def __init__(self, base_url: str, token: str, *, timeout: float = 5.0) -> None:
        if not isinstance(base_url, str) or not base_url:
            raise ValueError("base_url must be a non-empty HTTP or HTTPS URL")
        parsed = urlsplit(base_url)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            raise ValueError("base_url must be an HTTP or HTTPS URL")
        if parsed.query or parsed.fragment:
            raise ValueError("base_url must not contain a query or fragment")
        if not isinstance(token, str) or not token:
            raise ValueError("token must be a non-empty string")
        if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or timeout <= 0:
            raise ValueError("timeout must be greater than zero")

        self.base_url = base_url.rstrip("/")
        self.token = token
        self.timeout = float(timeout)

    def status(self) -> StatusSnapshot:
        """Return Wi-Fi, USB, pressed-key, and uptime status."""

        return StatusSnapshot.from_response(self._request("GET", "/api/v1/status"))

    def key_down(self, key: str) -> None:
        """Hold ``key`` until it is released or the safety timeout expires."""

        _validate_key(key)
        self._request("POST", "/api/v1/key/down", {"key": key})

    def key_up(self, key: str) -> None:
        """Release ``key``; releasing an already released key is safe."""

        _validate_key(key)
        self._request("POST", "/api/v1/key/up", {"key": key})

    def press(self, key: str, *, duration_ms: int | None = None) -> None:
        """Press and asynchronously release one key."""

        _validate_key(key)
        _validate_duration(duration_ms)
        self._request("POST", "/api/v1/key/press", _key_payload(key, duration_ms))

    def combo(self, keys: Sequence[str], *, duration_ms: int | None = None) -> None:
        """Press and asynchronously release up to six keys as one combo."""

        values = _validate_combo(keys)
        _validate_duration(duration_ms)
        payload: dict[str, Any] = {"keys": values}
        if duration_ms is not None:
            payload["duration_ms"] = duration_ms
        self._request("POST", "/api/v1/key/combo", payload)

    def release_all(self) -> None:
        """Release every key currently held by the device."""

        self._request("POST", "/api/v1/key/release-all")

    def websocket(self) -> "RemoteHIDWebSocket":
        """Return an async WebSocket session; connect with ``async with``."""

        return RemoteHIDWebSocket(self)

    @property
    def websocket_url(self) -> str:
        """The WebSocket endpoint URL derived from :attr:`base_url`."""

        parsed = urlsplit(self.base_url)
        scheme = "wss" if parsed.scheme == "https" else "ws"
        return f"{scheme}://{parsed.netloc}{parsed.path}/ws/v1/keyboard"

    def _url(self, path: str) -> str:
        return f"{self.base_url}{path}"

    def _request(
        self,
        method: str,
        path: str,
        payload: Mapping[str, Any] | None = None,
    ) -> Mapping[str, Any]:
        body = None
        headers = {
            "Accept": "application/json",
            "Authorization": f"Bearer {self.token}",
            "User-Agent": _USER_AGENT,
        }
        if payload is not None:
            body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
            headers["Content-Type"] = "application/json"
        request = Request(self._url(path), data=body, method=method, headers=headers)
        try:
            with urlopen(request, timeout=self.timeout) as response:
                raw_body = response.read()
                status_code = response.status
        except HTTPError as exc:
            raw_body = exc.read()
            response = _try_decode_error(raw_body)
            message = response.get("message") if response else str(exc)
            error = response.get("error") if response else None
            raise RemoteHIDError(
                str(message),
                status_code=exc.code,
                error=error if isinstance(error, str) else None,
                response=response,
            ) from exc
        except (OSError, URLError, TimeoutError) as exc:
            raise RemoteHIDConnectionError(f"Unable to reach Remote HID device: {exc}") from exc

        response = _decode_json(raw_body, context=f"HTTP {status_code} response")
        if response.get("ok") is not True:
            raise RemoteHIDProtocolError(
                "HTTP response did not report success", status_code=status_code, response=response
            )
        return response


def _key_payload(key: str, duration_ms: int | None) -> dict[str, Any]:
    payload: dict[str, Any] = {"key": key}
    if duration_ms is not None:
        payload["duration_ms"] = duration_ms
    return payload


def _try_decode_error(raw_body: bytes) -> Mapping[str, Any] | None:
    try:
        decoded = json.loads(raw_body.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None
    return decoded if isinstance(decoded, dict) else None


class RemoteHIDWebSocket:
    """Async WebSocket session returned by :meth:`RemoteHIDClient.websocket`."""

    def __init__(self, client: RemoteHIDClient) -> None:
        self._client = client
        self._socket: Any = None

    async def __aenter__(self) -> "RemoteHIDWebSocket":
        await self.connect()
        return self

    async def __aexit__(self, exc_type: Any, exc_value: Any, traceback: Any) -> None:
        await self.close()

    async def connect(self) -> "RemoteHIDWebSocket":
        """Open the authenticated WebSocket session."""

        if self._socket is not None:
            raise RuntimeError("WebSocket session is already connected")
        try:
            import websockets

            self._socket = await websockets.connect(
                self._client.websocket_url,
                additional_headers={
                    "Authorization": f"Bearer {self._client.token}",
                    "User-Agent": _USER_AGENT,
                },
                open_timeout=self._client.timeout,
                close_timeout=self._client.timeout,
            )
        except Exception as exc:
            status_code = getattr(exc, "status_code", None)
            raise RemoteHIDConnectionError(
                f"Unable to open Remote HID WebSocket: {exc}", status_code=status_code
            ) from exc
        return self

    async def close(self) -> None:
        """Close the session and let the firmware release held keys."""

        if self._socket is not None:
            socket, self._socket = self._socket, None
            await socket.close()

    async def key_down(self, key: str) -> None:
        """Hold ``key`` until it is released or the device safety timeout expires."""

        _validate_key(key)
        await self._command({"type": "key_down", "key": key})

    async def key_up(self, key: str) -> None:
        """Release ``key``."""

        _validate_key(key)
        await self._command({"type": "key_up", "key": key})

    async def release_all(self) -> None:
        """Release every key currently held by the device."""

        await self._command({"type": "release_all"})

    async def send(self, message: Mapping[str, Any]) -> Mapping[str, Any]:
        """Send a raw protocol message and return its successful response.

        This escape hatch is useful when a newer firmware adds a command before
        this client has a convenience method for it.
        """

        if not isinstance(message, Mapping):
            raise ValueError("message must be a mapping")
        return await self._command(dict(message))

    async def _command(self, message: Mapping[str, Any]) -> Mapping[str, Any]:
        if self._socket is None:
            raise RuntimeError("WebSocket session is not connected")
        try:
            await self._socket.send(json.dumps(message, separators=(",", ":")))
            raw_response = await self._socket.recv()
        except Exception as exc:
            raise RemoteHIDConnectionError(f"Remote HID WebSocket operation failed: {exc}") from exc
        response = _decode_json(
            raw_response if isinstance(raw_response, bytes) else str(raw_response).encode("utf-8"),
            context="WebSocket response",
        )
        if response.get("ok") is not True:
            error = response.get("error")
            message_text = response.get("message")
            raise RemoteHIDError(
                message_text if isinstance(message_text, str) else "WebSocket command failed",
                error=error if isinstance(error, str) else None,
                response=response,
            )
        return response
