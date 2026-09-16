import asyncio
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import pytest

from remote_hid_client import (
    RemoteHIDClient,
    RemoteHIDConnectionError,
    RemoteHIDError,
    RemoteHIDProtocolError,
    find_device_ip,
    find_device_ips,
)
from remote_hid_client import cli


class ApiHandler(BaseHTTPRequestHandler):
    requests = []

    def log_message(self, *_args):
        pass

    def _record(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        self.requests.append(
            {
                "method": self.command,
                "path": self.path,
                "authorization": self.headers.get("Authorization"),
                "user_agent": self.headers.get("User-Agent"),
                "content_type": self.headers.get("Content-Type"),
                "body": json.loads(body) if body else None,
            }
        )

    def _response(self, status, payload):
        encoded = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    def do_GET(self):
        self._record()
        if self.path in {"/prefix/api/v1/discovery", "/api/v1/discovery"}:
            self._response(
                200,
                {
                    "ok": True,
                    "device_type": "esp32-s3-remote-hid",
                    "discovery_id": "esp32-s3-remote-hid-v1",
                },
            )
            return
        if self.path == "/prefix/api/v1/status":
            self._response(
                200,
                {
                    "ok": True,
                    "wifi_connected": True,
                    "usb_mounted": False,
                    "pressed_keys": ["LEFT_CTRL", "A"],
                    "uptime_ms": 123,
                },
            )
            return
        self._response(404, {"ok": False, "error": "not_found", "message": "missing"})

    def do_POST(self):
        self._record()
        if self.path == "/prefix/api/v1/key/down" and self.requests[-1]["body"]["key"] == "FAIL":
            self._response(
                400,
                {"ok": False, "error": "unknown_key", "message": "unsupported"},
            )
            return
        self._response(200, {"ok": True})


@pytest.fixture
def http_server():
    ApiHandler.requests = []
    server = ThreadingHTTPServer(("127.0.0.1", 0), ApiHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_port}/prefix", ApiHandler.requests
    finally:
        server.shutdown()
        thread.join()


def test_rest_methods_send_contract_and_decode_status(http_server):
    base_url, requests = http_server
    client = RemoteHIDClient(base_url, "secret", timeout=2)

    status = client.status()
    client.key_down("A")
    client.key_up("A")
    client.press("ENTER", duration_ms=75)
    client.combo(["LEFT_CTRL", "C"])
    client.release_all()

    assert status.wifi_connected is True
    assert status.usb_mounted is False
    assert status.pressed_keys == ("LEFT_CTRL", "A")
    assert status.uptime_ms == 123
    assert [request["path"] for request in requests] == [
        "/prefix/api/v1/status",
        "/prefix/api/v1/key/down",
        "/prefix/api/v1/key/up",
        "/prefix/api/v1/key/press",
        "/prefix/api/v1/key/combo",
        "/prefix/api/v1/key/release-all",
    ]
    assert all(request["authorization"] == "Bearer secret" for request in requests)
    assert requests[1]["body"] == {"key": "A"}
    assert requests[3]["body"] == {"key": "ENTER", "duration_ms": 75}
    assert requests[4]["body"] == {"keys": ["LEFT_CTRL", "C"]}
    assert requests[-1]["body"] is None


def test_rest_error_exposes_firmware_error(http_server):
    base_url, _ = http_server
    client = RemoteHIDClient(base_url, "secret")

    with pytest.raises(RemoteHIDError) as caught:
        client.key_down("FAIL")

    assert caught.value.status_code == 400
    assert caught.value.error == "unknown_key"
    assert caught.value.message == "unsupported"
    assert caught.value.response["ok"] is False


def test_client_validates_inputs_without_network(http_server):
    base_url, requests = http_server
    client = RemoteHIDClient(base_url, "secret")

    with pytest.raises(ValueError):
        client.press("A", duration_ms=0)
    with pytest.raises(ValueError):
        client.press("A", duration_ms=True)
    with pytest.raises(ValueError):
        client.combo([])
    with pytest.raises(ValueError):
        client.combo(["A"] * 7)
    with pytest.raises(ValueError):
        client.key_down("")
    assert requests == []


def test_client_validates_base_url_and_status_protocol(http_server):
    with pytest.raises(ValueError):
        RemoteHIDClient("ftp://device", "secret")
    with pytest.raises(ValueError):
        RemoteHIDClient("http://device/path?token=secret", "secret")

    base_url, _ = http_server
    client = RemoteHIDClient(base_url, "secret")
    assert client.websocket_url == f"ws://{base_url.removeprefix('http://')}/ws/v1/keyboard"


def test_connection_errors_are_distinguishable(monkeypatch):
    def fail(*_args, **_kwargs):
        raise OSError("offline")

    monkeypatch.setattr("remote_hid_client.client.urlopen", fail)
    client = RemoteHIDClient("http://device", "secret")
    with pytest.raises(RemoteHIDConnectionError):
        client.status()


def test_status_protocol_errors_are_reported(http_server):
    class BadHandler(ApiHandler):
        def do_GET(self):
            self._record()
            self._response(200, {"ok": True, "wifi_connected": "yes"})

    server = ThreadingHTTPServer(("127.0.0.1", 0), BadHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        client = RemoteHIDClient(f"http://127.0.0.1:{server.server_port}", "secret")
        with pytest.raises(RemoteHIDProtocolError):
            client.status()
    finally:
        server.shutdown()
        thread.join()


def test_discovery_finds_matching_device_without_authentication():
    ApiHandler.requests = []
    server = ThreadingHTTPServer(("127.0.0.1", 0), ApiHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        cidr = "127.0.0.0/30"
        matches = find_device_ips(
            cidr,
            port=server.server_port,
            timeout=0.2,
            max_workers=4,
        )
        assert matches == ["127.0.0.1"]
        assert find_device_ip(cidr, port=server.server_port, timeout=0.2) == "127.0.0.1"
    finally:
        server.shutdown()
        thread.join()
    assert ApiHandler.requests
    assert all(request["authorization"] is None for request in ApiHandler.requests)


def test_discovery_validates_cidr_and_large_scans():
    with pytest.raises(ValueError, match="invalid LAN CIDR"):
        find_device_ips("not-a-cidr")
    with pytest.raises(ValueError, match="refusing to scan"):
        find_device_ips("192.178.0.0/16")


def test_discovery_ignores_a_nonmatching_http_service():
    class WrongHandler(ApiHandler):
        def do_GET(self):
            self._record()
            self._response(
                200,
                {"ok": True, "device_type": "other-device", "discovery_id": "other-v1"},
            )

    server = ThreadingHTTPServer(("127.0.0.1", 0), WrongHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        assert find_device_ips(
            "127.0.0.0/30",
            port=server.server_port,
            timeout=0.2,
            max_workers=4,
        ) == []
    finally:
        server.shutdown()
        thread.join()


def test_debug_helper_sends_keys_in_order(monkeypatch, capsys):
    calls = []

    class FakeClient:
        def __init__(self, base_url, token, *, timeout):
            calls.append(("init", base_url, token, timeout))

        def press(self, key, *, duration_ms=None):
            calls.append(("press", key, duration_ms))

    monkeypatch.setattr(cli, "RemoteHIDClient", FakeClient)
    assert (
        cli.main(
            [
                "--ip",
                "192.168.1.42",
                "--token",
                "secret",
                "--duration-ms",
                "75",
                "A",
                "ENTER",
            ]
        )
        == 0
    )

    assert calls == [
        ("init", "http://192.168.1.42", "secret", 5.0),
        ("press", "A", 75),
        ("press", "ENTER", 75),
    ]
    assert "secret" not in capsys.readouterr().out


def test_websocket_commands_and_authentication():
    websockets = pytest.importorskip("websockets")
    from websockets.asyncio.server import serve

    async def run():
        received = []

        async def handler(socket):
            assert socket.request.headers["Authorization"] == "Bearer secret"
            async for raw_message in socket:
                received.append(json.loads(raw_message))
                await socket.send(json.dumps({"ok": True}))

        async with serve(handler, "127.0.0.1", 0) as server:
            port = server.sockets[0].getsockname()[1]
            client = RemoteHIDClient(f"http://127.0.0.1:{port}", "secret")
            async with client.websocket() as keyboard:
                await keyboard.key_down("A")
                await keyboard.key_up("A")
                await keyboard.release_all()
                response = await keyboard.send({"type": "future_command"})
                assert response["ok"] is True
        return received

    assert asyncio.run(run()) == [
        {"type": "key_down", "key": "A"},
        {"type": "key_up", "key": "A"},
        {"type": "release_all"},
        {"type": "future_command"},
    ]


def test_websocket_error_is_exposed():
    websockets = pytest.importorskip("websockets")
    from websockets.asyncio.server import serve

    async def run():
        async def handler(socket):
            await socket.recv()
            await socket.send(
                json.dumps(
                    {"ok": False, "error": "unknown_command", "message": "nope"}
                )
            )

        async with serve(handler, "127.0.0.1", 0) as server:
            port = server.sockets[0].getsockname()[1]
            client = RemoteHIDClient(f"http://127.0.0.1:{port}", "secret")
            async with client.websocket() as keyboard:
                with pytest.raises(RemoteHIDError) as caught:
                    await keyboard.send({"type": "bad"})
        return caught.value

    error = asyncio.run(run())
    assert error.error == "unknown_command"
    assert error.message == "nope"
