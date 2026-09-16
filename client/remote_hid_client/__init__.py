"""Python client for the ESP32-S3 Remote HID Keyboard API."""

from .client import (
    RemoteHIDClient,
    RemoteHIDConnectionError,
    RemoteHIDError,
    RemoteHIDProtocolError,
    RemoteHIDWebSocket,
    StatusSnapshot,
)
from .discovery import (
    DISCOVERY_DEVICE_TYPE,
    DISCOVERY_ID,
    DISCOVERY_PATH,
    find_device_ip,
    find_device_ips,
)

__all__ = [
    "RemoteHIDClient",
    "RemoteHIDConnectionError",
    "RemoteHIDError",
    "RemoteHIDProtocolError",
    "RemoteHIDWebSocket",
    "StatusSnapshot",
    "DISCOVERY_DEVICE_TYPE",
    "DISCOVERY_ID",
    "DISCOVERY_PATH",
    "find_device_ip",
    "find_device_ips",
]
