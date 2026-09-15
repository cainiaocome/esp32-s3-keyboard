import pytest
import os


def pytest_configure(config):
    config.addinivalue_line("markers", "hardware: requires an ESP32-S3 and a reachable API")


@pytest.fixture
def hardware_api_url(monkeypatch):
    url = os.environ.get("REMOTE_HID_API_URL")
    if not url:
        pytest.skip("set REMOTE_HID_API_URL to run hardware API tests")
    return url.rstrip("/")
