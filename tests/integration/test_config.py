from pathlib import Path
import os
import stat
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts" / "generate_sdkconfig.py"


def run_generator(tmp_path: Path, contents: str) -> str:
    env_file = tmp_path / ".env"
    output_file = tmp_path / "sdkconfig.defaults.local"
    env_file.write_text(contents, encoding="utf-8")
    subprocess.run(
        [sys.executable, str(GENERATOR), "--env-file", str(env_file), "--output", str(output_file)],
        check=True,
    )
    return output_file.read_text(encoding="utf-8")


def test_env_values_are_mapped_to_kconfig(tmp_path: Path) -> None:
    generated = run_generator(
        tmp_path,
        'WIFI_SSID="lab network"\n'
        "WIFI_PASSWORD=secret\n"
        "API_TOKEN=token-value\n"
        "KEY_HOLD_TIMEOUT_MS=12000\n"
        "KEY_PRESS_DURATION_MS=75\n",
    )
    assert 'CONFIG_REMOTE_HID_WIFI_SSID="lab network"' in generated
    assert 'CONFIG_REMOTE_HID_WIFI_PASSWORD="secret"' in generated
    assert 'CONFIG_REMOTE_HID_API_TOKEN="token-value"' in generated
    assert "CONFIG_REMOTE_HID_KEY_HOLD_TIMEOUT_MS=12000" in generated
    assert "CONFIG_REMOTE_HID_KEY_PRESS_DURATION_MS=75" in generated


def test_env_comments_and_kconfig_escaping(tmp_path: Path) -> None:
    generated = run_generator(
        tmp_path,
        "# ignored\n"
        'WIFI_SSID=ssid\\name\n'
        'WIFI_PASSWORD=pass"word\n'
        "API_TOKEN=abc\n",
    )
    assert 'CONFIG_REMOTE_HID_WIFI_SSID="ssid\\\\name"' in generated
    assert 'CONFIG_REMOTE_HID_WIFI_PASSWORD="pass\\"word"' in generated


def test_timing_ranges_fail_fast_and_output_is_private(tmp_path: Path) -> None:
    env_file = tmp_path / ".env"
    output_file = tmp_path / "sdkconfig.defaults.local"
    env_file.write_text(
        "WIFI_SSID=ssid\nAPI_TOKEN=token\nKEY_HOLD_TIMEOUT_MS=500\n",
        encoding="utf-8",
    )
    result = subprocess.run(
        [sys.executable, str(GENERATOR), "--env-file", str(env_file), "--output", str(output_file)],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode != 0
    assert "KEY_HOLD_TIMEOUT_MS must be an integer from 1000 to 600000" in result.stderr

    output_file.write_text("old", encoding="utf-8")
    os.chmod(output_file, 0o644)
    env_file.write_text("WIFI_SSID=ssid\nAPI_TOKEN=token\n", encoding="utf-8")
    subprocess.run(
        [sys.executable, str(GENERATOR), "--env-file", str(env_file), "--output", str(output_file)],
        check=True,
    )
    assert stat.S_IMODE(output_file.stat().st_mode) == 0o600
