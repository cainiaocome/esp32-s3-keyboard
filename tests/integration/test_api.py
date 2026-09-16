import json
import os
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

import pytest


def target_config():
    base_url = os.environ.get("REMOTE_HID_API_URL")
    token = os.environ.get("REMOTE_HID_API_TOKEN")
    if not base_url or not token:
        pytest.skip("set REMOTE_HID_API_URL and REMOTE_HID_API_TOKEN for hardware API tests")
    return base_url.rstrip("/"), token


def api_call(base_url, token, path, method="GET", payload=None, auth_token=None, raw_body=None):
    body = raw_body
    if body is None and payload is not None:
        body = json.dumps(payload).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if auth_token is not None:
        headers["Authorization"] = f"Bearer {auth_token}"
    request = Request(
        f"{base_url}{path}", data=body, method=method, headers=headers
    )
    try:
        with urlopen(request, timeout=5) as response:
            return response.status, json.loads(response.read().decode("utf-8"))
    except HTTPError as error:
        return error.code, json.loads(error.read().decode("utf-8"))
    except URLError as error:
        pytest.fail(f"hardware API is unreachable: {error}")


@pytest.mark.hardware
def test_rest_actions_and_error_contract():
    base_url, token = target_config()
    try:
        status, response = api_call(base_url, token, "/api/v1/status", auth_token=token)
        assert status == 200 and response["ok"] is True

        status, response = api_call(
            base_url, token, "/api/v1/key/down", "POST", {"key": "LEFT_CTRL"}, token
        )
        assert status == 200 and response["ok"] is True
        status, response = api_call(
            base_url, token, "/api/v1/key/up", "POST", {"key": "LEFT_CTRL"}, token
        )
        assert status == 200 and response["ok"] is True

        status, response = api_call(
            base_url, token, "/api/v1/key/press", "POST", {"key": "ENTER", "duration_ms": 1}, token
        )
        assert status == 200 and response["ok"] is True
        status, response = api_call(
            base_url,
            token,
            "/api/v1/key/combo",
            "POST",
            {"keys": ["LEFT_CTRL", "C"], "duration_ms": 1},
            token,
        )
        assert status == 200 and response["ok"] is True

        status, response = api_call(
            base_url, token, "/api/v1/key/down", "POST", {"key": "NOPE"}, token
        )
        assert status == 400 and response["error"] == "unknown_key"
        status, response = api_call(
            base_url, token, "/api/v1/key/down", "POST", auth_token=token, raw_body=b"{"
        )
        assert status == 400 and response["error"] == "invalid_json"
        status, response = api_call(
            base_url, token, "/api/v1/key/down", "POST", auth_token=token
        )
        assert status == 400 and response["error"] == "missing_body"
        status, response = api_call(
            base_url,
            token,
            "/api/v1/key/press",
            "POST",
            {"key": "A", "duration_ms": 1001},
            token,
        )
        assert status == 400 and response["error"] == "invalid_duration"
        status, response = api_call(
            base_url,
            token,
            "/api/v1/key/down",
            "POST",
            auth_token=token,
            raw_body=b"x" * 1025,
        )
        assert status == 413 and response["error"] == "body_too_large"
        status, response = api_call(base_url, token, "/api/v1/status", auth_token=None)
        assert status == 401 and response["error"] == "unauthorized"
        status, response = api_call(
            base_url, token, "/api/v1/status", auth_token="wrong-token"
        )
        assert status == 401 and response["error"] == "unauthorized"
    finally:
        api_call(base_url, token, "/api/v1/key/release-all", "POST", auth_token=token)
