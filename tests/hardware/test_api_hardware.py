import json
import os
from urllib.error import URLError
from urllib.request import Request, urlopen

import pytest


def api_call(base_url: str, token: str, path: str, method: str = "GET", payload=None):
    data = None if payload is None else json.dumps(payload).encode("utf-8")
    request = Request(
        f"{base_url}{path}",
        data=data,
        method=method,
        headers={
            "Authorization": f"Bearer {token}",
            "Content-Type": "application/json",
        },
    )
    try:
        with urlopen(request, timeout=5) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except URLError as exc:
        pytest.fail(f"hardware API is unreachable: {exc}")


@pytest.mark.hardware
def test_status_and_key_lifecycle(hardware_api_url):
    token = os.environ.get("REMOTE_HID_API_TOKEN")
    if not token:
        pytest.skip("set REMOTE_HID_API_TOKEN to run hardware API tests")

    status, response = api_call(hardware_api_url, token, "/api/v1/status")
    assert status == 200
    assert response["ok"] is True
    assert "pressed_keys" in response

    status, response = api_call(
        hardware_api_url, token, "/api/v1/key/down", "POST", {"key": "A"}
    )
    assert status == 200 and response["ok"] is True

    status, response = api_call(
        hardware_api_url, token, "/api/v1/key/up", "POST", {"key": "A"}
    )
    assert status == 200 and response["ok"] is True

    status, response = api_call(
        hardware_api_url, token, "/api/v1/key/release-all", "POST", {}
    )
    assert status == 200 and response["ok"] is True
