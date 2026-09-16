import asyncio
import json
import os
from urllib.request import Request, urlopen

import pytest


def target_config():
    base_url = os.environ.get("REMOTE_HID_API_URL")
    token = os.environ.get("REMOTE_HID_API_TOKEN")
    if not base_url or not token:
        pytest.skip("set REMOTE_HID_API_URL and REMOTE_HID_API_TOKEN for WebSocket tests")
    ws_url = base_url.rstrip("/").replace("https://", "wss://").replace("http://", "ws://")
    return f"{ws_url}/ws/v1/keyboard", base_url.rstrip("/"), token


def websockets_module():
    return pytest.importorskip("websockets")


def status_keys(base_url, token):
    request = Request(
        f"{base_url}/api/v1/status", headers={"Authorization": f"Bearer {token}"}
    )
    with urlopen(request, timeout=5) as response:
        return json.loads(response.read().decode("utf-8"))["pressed_keys"]


@pytest.mark.hardware
def test_authenticated_messages_invalid_messages_and_disconnect_cleanup():
    websockets = websockets_module()
    ws_url, base_url, token = target_config()

    async def run():
        async with websockets.connect(
            ws_url, additional_headers={"Authorization": f"Bearer {token}"}
        ) as socket:
            await socket.send(json.dumps({"type": "key_down", "key": "LEFT_SHIFT"}))
            assert json.loads(await socket.recv())["ok"] is True
            await socket.send(json.dumps({"type": "key_up", "key": "LEFT_SHIFT"}))
            assert json.loads(await socket.recv())["ok"] is True
            await socket.send("{")
            assert json.loads(await socket.recv())["error"] == "invalid_json"
            await socket.send(json.dumps({"type": "not-a-command"}))
            assert json.loads(await socket.recv())["error"] == "unknown_command"
            await socket.send(json.dumps({"type": "key_down", "key": "LEFT_GUI"}))
            assert json.loads(await socket.recv())["ok"] is True

    asyncio.run(run())
    assert "LEFT_GUI" not in status_keys(base_url, token)


@pytest.mark.hardware
def test_websocket_rejects_missing_or_wrong_authentication():
    websockets = websockets_module()
    ws_url, _, token = target_config()

    async def run():
        with pytest.raises(websockets.exceptions.InvalidStatus):
            await websockets.connect(ws_url)
        with pytest.raises(websockets.exceptions.InvalidStatus):
            await websockets.connect(
                ws_url, additional_headers={"Authorization": "Bearer wrong-token"}
            )

    asyncio.run(run())
