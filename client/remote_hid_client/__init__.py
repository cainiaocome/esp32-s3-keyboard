"""Python client for the ESP32-S3 Remote HID Keyboard API."""

from .client import (
    RemoteHIDClient,
    RemoteHIDConnectionError,
    RemoteHIDError,
    RemoteHIDProtocolError,
    RemoteHIDWebSocket,
    StatusSnapshot,
)

__all__ = [
    "RemoteHIDClient",
    "RemoteHIDConnectionError",
    "RemoteHIDError",
    "RemoteHIDProtocolError",
    "RemoteHIDWebSocket",
    "StatusSnapshot",
]
