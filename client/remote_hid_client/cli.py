"""Command-line helpers for debugging a Remote HID device."""

from __future__ import annotations

import argparse
import os
import sys
import time
from typing import Sequence

from .client import RemoteHIDClient, RemoteHIDError
from .discovery import find_device_ip


def _base_url(ip: str) -> str:
    host = ip.strip()
    if not host:
        raise ValueError("ip must be a non-empty host or IP address")
    if ":" in host and not host.startswith("["):
        host = f"[{host}]"
    return f"http://{host}"


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Send a sequence of one-key presses to an ESP32-S3 Remote HID device."
    )
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument(
        "--cidr",
        help="LAN CIDR to scan for the ESP32, for example 192.178.2.0/24",
    )
    target.add_argument(
        "--ip",
        help="ESP32 host or IP address; bypasses discovery",
    )
    parser.add_argument(
        "--token",
        default=None,
        help="API token; prefer REMOTE_HID_API_TOKEN or API_TOKEN in the environment",
    )
    parser.add_argument(
        "--duration-ms",
        type=int,
        default=None,
        help="firmware press duration from 1 to 1000 ms (default: firmware default)",
    )
    parser.add_argument(
        "--delay-ms",
        type=int,
        default=0,
        help="delay between press commands, in milliseconds (default: 0)",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=5.0,
        help="per-request timeout in seconds (default: 5)",
    )
    parser.add_argument(
        "--discovery-timeout",
        type=float,
        default=0.25,
        help="per-address discovery timeout in seconds (default: 0.25)",
    )
    parser.add_argument("keys", nargs="+", help="key names to press in order")
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    """Run the sequence sender and return a process exit code."""

    parser = _parser()
    args = parser.parse_args(argv)
    if args.delay_ms < 0:
        parser.error("--delay-ms must not be negative")
    token = args.token or os.environ.get("REMOTE_HID_API_TOKEN") or os.environ.get("API_TOKEN")
    if not token:
        parser.error("provide --token, REMOTE_HID_API_TOKEN, or API_TOKEN")

    try:
        if args.cidr:
            ip = find_device_ip(args.cidr, timeout=args.discovery_timeout)
            if ip is None:
                raise ValueError(f"no Remote HID device found in {args.cidr}")
            print(f"Discovered Remote HID device at {ip}")
        else:
            ip = args.ip
        base_url = _base_url(ip)
        client = RemoteHIDClient(base_url, token, timeout=args.timeout)
        print(f"Sending {len(args.keys)} key press(es) to {base_url}...")
        for index, key in enumerate(args.keys):
            client.press(key, duration_ms=args.duration_ms)
            print(f"  {index + 1}/{len(args.keys)}: {key}")
            if args.delay_ms and index + 1 < len(args.keys):
                time.sleep(args.delay_ms / 1000)
    except (RemoteHIDError, ValueError) as exc:
        print(f"Remote HID error: {exc}", file=sys.stderr)
        return 1
    return 0
