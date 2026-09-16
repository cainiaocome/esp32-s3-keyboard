"""LAN discovery helpers for Remote HID devices."""

from __future__ import annotations

import ipaddress
import json
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


DISCOVERY_PATH = "/api/v1/discovery"
DISCOVERY_DEVICE_TYPE = "esp32-s3-remote-hid"
DISCOVERY_ID = "esp32-s3-remote-hid-v1"
DEFAULT_DISCOVERY_TIMEOUT = 0.25
DEFAULT_DISCOVERY_WORKERS = 64
DEFAULT_DISCOVERY_MAX_HOSTS = 4096


def find_device_ips(
    cidr: str,
    *,
    port: int = 80,
    timeout: float = DEFAULT_DISCOVERY_TIMEOUT,
    max_workers: int = DEFAULT_DISCOVERY_WORKERS,
    max_hosts: int = DEFAULT_DISCOVERY_MAX_HOSTS,
) -> list[str]:
    """Return all matching Remote HID IP addresses in ``cidr``.

    Discovery requests are unauthenticated ``GET`` requests to the
    read-only discovery endpoint. Every address is probed concurrently, with
    connection failures and non-matching HTTP services ignored.

    The default host limit prevents an accidental scan of an Internet-sized
    network. Set a larger explicit ``max_hosts`` only when that is intentional.
    """

    network = _parse_network(cidr)
    host_count = network.num_addresses
    if network.version == 4 and network.prefixlen < 31:
        host_count -= 2
    _validate_scan_options(port, timeout, max_workers, max_hosts, host_count)
    hosts = [str(address) for address in network.hosts()]
    if not hosts:
        return []

    matches: list[str] = []
    worker_count = min(max_workers, len(hosts))
    with ThreadPoolExecutor(max_workers=worker_count) as executor:
        futures = {
            executor.submit(_probe, host, port, timeout): host for host in hosts
        }
        for future in as_completed(futures):
            if future.result():
                matches.append(futures[future])
    return sorted(matches, key=ipaddress.ip_address)


def find_device_ip(
    cidr: str,
    *,
    port: int = 80,
    timeout: float = DEFAULT_DISCOVERY_TIMEOUT,
    max_workers: int = DEFAULT_DISCOVERY_WORKERS,
    max_hosts: int = DEFAULT_DISCOVERY_MAX_HOSTS,
) -> str | None:
    """Return the first matching IP in ``cidr``, or ``None`` if not found.

    The discovery marker is intentionally fixed, so this function cannot
    distinguish multiple Remote HID boards. Use :func:`find_device_ips` when
    more than one board may be present.
    """

    matches = find_device_ips(
        cidr,
        port=port,
        timeout=timeout,
        max_workers=max_workers,
        max_hosts=max_hosts,
    )
    return matches[0] if matches else None


def _parse_network(cidr: str) -> ipaddress.IPv4Network | ipaddress.IPv6Network:
    try:
        return ipaddress.ip_network(cidr, strict=False)
    except ValueError as exc:
        raise ValueError(f"invalid LAN CIDR: {cidr!r}") from exc


def _validate_scan_options(
    port: int, timeout: float, max_workers: int, max_hosts: int, host_count: int
) -> None:
    if type(port) is not int or not 1 <= port <= 65535:
        raise ValueError("port must be an integer from 1 to 65535")
    if not isinstance(timeout, (int, float)) or isinstance(timeout, bool) or timeout <= 0:
        raise ValueError("timeout must be greater than zero")
    if type(max_workers) is not int or max_workers < 1:
        raise ValueError("max_workers must be a positive integer")
    if type(max_hosts) is not int or max_hosts < 1:
        raise ValueError("max_hosts must be a positive integer")
    if host_count > max_hosts:
        raise ValueError(
            f"CIDR contains {host_count} hosts; refusing to scan more than {max_hosts}"
        )


def _probe(host: str, port: int, timeout: float) -> bool:
    url_host = f"[{host}]" if ":" in host else host
    url = f"http://{url_host}:{port}{DISCOVERY_PATH}"
    request = Request(
        url,
        method="GET",
        headers={
            "Accept": "application/json",
            "User-Agent": "esp32-s3-remote-hid-discovery/0.1",
        },
    )
    try:
        with urlopen(request, timeout=timeout) as response:
            if response.status != 200:
                return False
            payload: Any = json.loads(response.read().decode("utf-8"))
    except (HTTPError, OSError, URLError, TimeoutError, UnicodeDecodeError, json.JSONDecodeError):
        return False
    return (
        isinstance(payload, dict)
        and payload.get("ok") is True
        and payload.get("device_type") == DISCOVERY_DEVICE_TYPE
        and payload.get("discovery_id") == DISCOVERY_ID
    )
