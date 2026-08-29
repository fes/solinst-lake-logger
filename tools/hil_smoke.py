#!/usr/bin/env python3
"""Non-destructive HTTP smoke test for a running Opta or Giga lake logger."""

import argparse
import json
import math
import sys
import urllib.error
import urllib.request


class SmokeFailure(Exception):
    """An actionable HIL validation failure."""


def _fail(endpoint, message):
    raise SmokeFailure(f"{endpoint}: {message}")


def _require_key(payload, key, endpoint):
    if key not in payload:
        _fail(endpoint, f"missing required field {key!r}")
    return payload[key]


def _require_bool(payload, key, endpoint):
    value = _require_key(payload, key, endpoint)
    if type(value) is not bool:
        _fail(endpoint, f"{key} must be a boolean, got {type(value).__name__}")
    return value


def _require_string(payload, key, endpoint, *, nonempty=False):
    value = _require_key(payload, key, endpoint)
    if not isinstance(value, str):
        _fail(endpoint, f"{key} must be a string, got {type(value).__name__}")
    if nonempty and not value.strip():
        _fail(endpoint, f"{key} must not be empty")
    return value


def _require_integer(payload, key, endpoint, *, minimum=None, maximum=None):
    value = _require_key(payload, key, endpoint)
    if type(value) is not int:
        _fail(endpoint, f"{key} must be an integer, got {type(value).__name__}")
    if minimum is not None and value < minimum:
        _fail(endpoint, f"{key} must be at least {minimum}, got {value}")
    if maximum is not None and value > maximum:
        _fail(endpoint, f"{key} must be at most {maximum}, got {value}")
    return value


def _require_finite(payload, key, endpoint):
    value = _require_key(payload, key, endpoint)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        _fail(endpoint, f"{key} must be numeric, got {type(value).__name__}")
    if not math.isfinite(value):
        _fail(endpoint, f"{key} must be finite, got {value!r}")
    return value


def _validate_power(payload, prefix, endpoint):
    present = _require_bool(payload, f"{prefix}_monitor_present", endpoint)
    valid = _require_bool(payload, f"{prefix}_monitor_valid", endpoint)
    if valid and not present:
        _fail(endpoint, f"{prefix} cannot be valid when the monitor is absent")

    measurement_keys = (
        f"{prefix}_voltage_v",
        f"{prefix}_current_a",
        f"{prefix}_power_w",
    )
    if valid:
        for key in measurement_keys:
            _require_finite(payload, key, endpoint)
    else:
        for key in measurement_keys:
            if _require_key(payload, key, endpoint) is not None:
                _fail(endpoint, f"{key} must be null when {prefix} is invalid")


def _validate_weather(payload, prefix, endpoint):
    enabled = _require_bool(payload, f"{prefix}_enabled", endpoint)
    present = _require_bool(payload, f"{prefix}_present", endpoint)
    valid = _require_bool(payload, f"{prefix}_valid", endpoint)
    if present and not enabled:
        _fail(endpoint, f"{prefix} cannot be present when weather is disabled")
    if valid and (not enabled or not present):
        _fail(endpoint, f"{prefix} cannot be valid unless enabled and present")

    measurement_keys = (
        f"{prefix}_air_temperature_c",
        f"{prefix}_relative_humidity_pct",
        f"{prefix}_barometric_pressure_hpa",
        f"{prefix}_wind_speed_m_s",
        f"{prefix}_wind_direction_deg",
        f"{prefix}_rainfall_mm",
        f"{prefix}_light_lux",
    )
    if valid:
        for key in measurement_keys:
            _require_finite(payload, key, endpoint)
    else:
        for key in measurement_keys:
            value = _require_key(payload, key, endpoint)
            if value is not None:
                _require_finite(payload, key, endpoint)


def validate_status(payload, expected_device_id=None, expected_board_profile=None):
    endpoint = "/status"
    if not isinstance(payload, dict):
        _fail(endpoint, f"response must be a JSON object, got {type(payload).__name__}")

    device_id = _require_string(payload, "device_id", endpoint, nonempty=True)
    if expected_device_id is not None and device_id != expected_device_id:
        _fail(
            endpoint,
            f"device_id mismatch: expected {expected_device_id!r}, got {device_id!r}",
        )

    board_profile = _require_string(payload, "board_profile", endpoint, nonempty=True)
    if board_profile not in ("opta-solinst", "giga-site"):
        _fail(endpoint, f"unsupported board_profile {board_profile!r}")
    if expected_board_profile is not None and board_profile != expected_board_profile:
        _fail(
            endpoint,
            f"board_profile mismatch: expected {expected_board_profile!r}, "
            f"got {board_profile!r}",
        )
    channel_count = _require_integer(
        payload, "rs485_channel_count", endpoint, minimum=1, maximum=2
    )
    expected_channels = 1 if board_profile == "opta-solinst" else 2
    if channel_count != expected_channels:
        _fail(endpoint, "rs485_channel_count does not match board_profile")
    display_behavior = _require_string(
        payload, "display_behavior", endpoint, nonempty=True
    )
    expected_display = (
        "wake_on_demand"
        if board_profile == "opta-solinst"
        else "persistent_epaper"
    )
    if display_behavior != expected_display:
        _fail(endpoint, "display_behavior does not match board_profile")
    display_present = _require_bool(payload, "display_present", endpoint)
    _require_bool(payload, "display_awake", endpoint)
    _require_integer(payload, "display_refresh_count", endpoint, minimum=0)
    _require_integer(payload, "display_recovery_count", endpoint, minimum=0)
    _require_string(payload, "last_display_refresh_age", endpoint)
    if board_profile == "giga-site" and not display_present:
        _fail(endpoint, "configured Giga e-paper must report display_present")
    if _require_string(payload, "site_health", endpoint) not in (
        "healthy",
        "degraded",
        "critical",
    ):
        _fail(endpoint, "site_health must be healthy, degraded, or critical")

    _require_bool(payload, "wifi_connected", endpoint)
    if not _require_bool(payload, "http_server_started", endpoint):
        _fail(endpoint, "http_server_started must be true when /status is reachable")
    _require_integer(payload, "uptime_s", endpoint, minimum=0)
    clock_valid = _require_bool(payload, "clock_valid", endpoint)
    _require_bool(payload, "clock_valid_check", endpoint)
    clock_now = _require_string(payload, "clock_now_utc", endpoint)
    if clock_valid and not clock_now.strip():
        _fail(endpoint, "clock_now_utc must not be empty when the clock is valid")
    ntp_attempted = _require_bool(payload, "ntp_attempted", endpoint)
    _require_bool(payload, "last_ntp_attempt_succeeded", endpoint)
    ntp_attempt_age = _require_string(payload, "last_ntp_attempt_age", endpoint)
    if ntp_attempted and not ntp_attempt_age.strip():
        _fail(endpoint, "last_ntp_attempt_age must not be empty after an NTP attempt")
    _require_integer(payload, "ntp_cooldown_remaining_ms", endpoint, minimum=0)
    _require_integer(payload, "consecutive_ntp_failures", endpoint, minimum=0)

    sensor_found = _require_bool(payload, "sensor_found", endpoint)
    modbus_id = _require_integer(payload, "modbus_id", endpoint, minimum=0, maximum=247)
    if sensor_found != (modbus_id > 0):
        _fail(endpoint, "sensor_found must agree with whether modbus_id is nonzero")
    discovery_attempted = _require_bool(
        payload, "sensor_discovery_attempted", endpoint
    )
    discovery_attempts = _require_integer(
        payload, "sensor_discovery_attempt_count", endpoint, minimum=0
    )
    if discovery_attempted != (discovery_attempts > 0):
        _fail(
            endpoint,
            "sensor_discovery_attempted must agree with sensor_discovery_attempt_count",
        )
    _require_bool(payload, "last_sensor_discovery_attempt_succeeded", endpoint)
    _require_string(payload, "last_sensor_discovery_attempt_age", endpoint)
    _require_integer(
        payload, "sensor_discovery_cooldown_remaining_ms", endpoint, minimum=0
    )
    discovery_failures = _require_integer(
        payload, "consecutive_sensor_discovery_failures", endpoint, minimum=0
    )
    if sensor_found and discovery_failures != 0:
        _fail(endpoint, "consecutive sensor discovery failures must reset on success")

    _require_bool(payload, "cached_probe_valid", endpoint)
    _require_integer(payload, "successful_probe_reads", endpoint, minimum=0)
    _require_integer(payload, "failed_probe_reads", endpoint, minimum=0)
    _require_integer(payload, "successful_uploads", endpoint, minimum=0)
    _require_integer(payload, "failed_uploads", endpoint, minimum=0)
    permanent_rejections = _require_integer(
        payload, "permanent_upload_rejections", endpoint, minimum=0
    )
    permanent_backlog_drops = _require_integer(
        payload, "permanent_backlog_drops", endpoint, minimum=0
    )
    if permanent_backlog_drops > permanent_rejections:
        _fail(
            endpoint,
            "permanent_backlog_drops cannot exceed permanent_upload_rejections",
        )
    permanent_status = _require_integer(
        payload, "last_permanent_upload_rejection_status", endpoint, minimum=0
    )
    permanent_error = _require_string(
        payload, "last_permanent_upload_rejection_error", endpoint
    )
    permanent_utc = _require_string(
        payload, "last_permanent_upload_rejection_utc", endpoint
    )
    rejected_reading_utc = _require_string(
        payload, "last_permanent_upload_rejected_reading_utc", endpoint
    )
    if permanent_rejections > 0 and (
        permanent_status < 400
        or not permanent_error.strip()
        or not permanent_utc.strip()
        or not rejected_reading_utc.strip()
    ):
        _fail(
            endpoint,
            "permanent rejection diagnostics must be populated after a rejection",
        )
    _require_integer(payload, "backlog_count", endpoint, minimum=0)
    _require_integer(payload, "dropped_backlog_entries", endpoint, minimum=0)
    if not _require_bool(payload, "power_snapshot_initialized", endpoint):
        _fail(endpoint, "power snapshot must be initialized during startup")
    _require_string(payload, "power_snapshot_age", endpoint, nonempty=True)
    _require_integer(payload, "power_poll_interval_ms", endpoint, minimum=1000)
    _validate_power(payload, "live_battery_output", endpoint)
    _validate_power(payload, "live_solar_input", endpoint)
    _validate_weather(payload, "live_weather", endpoint)
    return device_id


def validate_probe(payload):
    endpoint = "/probe"
    if not isinstance(payload, dict):
        _fail(endpoint, f"response must be a JSON object, got {type(payload).__name__}")
    if not _require_bool(payload, "ok", endpoint):
        _fail(endpoint, "ok is false; the live probe did not succeed")

    _require_string(payload, "timestamp_utc", endpoint, nonempty=True)
    _require_finite(payload, "water_level_m", endpoint)
    _require_finite(payload, "temperature_c", endpoint)
    _validate_power(payload, "battery_output", endpoint)
    _validate_power(payload, "solar_input", endpoint)
    _validate_weather(payload, "weather", endpoint)
    units = _require_key(payload, "units", endpoint)
    if not isinstance(units, dict):
        _fail(endpoint, f"units must be an object, got {type(units).__name__}")
    if units.get("level") != "m" or units.get("temperature") != "C":
        _fail(endpoint, "units must report level='m' and temperature='C'")


def fetch_json(base_url, endpoint, timeout=10.0, opener=urllib.request.urlopen):
    url = f"{base_url.rstrip('/')}{endpoint}"
    request = urllib.request.Request(
        url, headers={"Accept": "application/json", "User-Agent": "lake-logger-hil/1"}
    )
    try:
        with opener(request, timeout=timeout) as response:
            status = response.getcode()
            content_type = response.headers.get_content_type()
            body = response.read()
    except urllib.error.HTTPError as exc:
        try:
            detail = exc.read(512).decode("utf-8", errors="replace").strip()
        finally:
            exc.close()
        _fail(endpoint, f"HTTP {exc.code} from {url}" + (f": {detail}" if detail else ""))
    except (urllib.error.URLError, TimeoutError, OSError) as exc:
        _fail(endpoint, f"request to {url} failed: {exc}")

    if status < 200 or status >= 300:
        _fail(endpoint, f"expected HTTP 2xx from {url}, got {status}")
    if content_type != "application/json":
        _fail(endpoint, f"expected application/json from {url}, got {content_type!r}")
    try:
        return json.loads(body)
    except (json.JSONDecodeError, UnicodeDecodeError) as exc:
        _fail(endpoint, f"invalid JSON from {url}: {exc}")


def run_smoke(
    base_url,
    expected_device_id=None,
    timeout=10.0,
    expected_board_profile=None,
):
    status = fetch_json(base_url, "/status", timeout)
    device_id = validate_status(
        status, expected_device_id, expected_board_profile
    )
    probe = fetch_json(base_url, "/probe", timeout)
    validate_probe(probe)
    return device_id


def main(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            "Run status validation and one live probe against an Opta or Giga logger. "
            "This tool never calls /reset or backlog-management endpoints."
        )
    )
    parser.add_argument("base_url", help="Logger URL, for example http://192.168.1.50")
    parser.add_argument(
        "--expected-device-id", help="Fail if /status reports a different device_id"
    )
    parser.add_argument(
        "--expected-board-profile",
        choices=("opta-solinst", "giga-site"),
        help="Fail if /status reports a different board profile",
    )
    parser.add_argument(
        "--timeout", type=float, default=10.0, help="Per-request timeout in seconds"
    )
    args = parser.parse_args(argv)
    if args.timeout <= 0:
        parser.error("--timeout must be greater than zero")

    try:
        device_id = run_smoke(
            args.base_url,
            args.expected_device_id,
            args.timeout,
            args.expected_board_profile,
        )
    except SmokeFailure as exc:
        print(f"HIL smoke FAILED: {exc}", file=sys.stderr)
        return 1
    print(f"HIL smoke passed for device {device_id!r}: /status and /probe are valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())
