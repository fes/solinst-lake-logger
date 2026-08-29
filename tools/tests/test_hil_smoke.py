import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from tools import hil_smoke


def power(prefix, present=True, valid=True):
    values = {
        f"{prefix}_monitor_present": present,
        f"{prefix}_monitor_valid": valid,
        f"{prefix}_voltage_v": 12.7 if valid else None,
        f"{prefix}_current_a": 0.2 if valid else None,
        f"{prefix}_power_w": 2.54 if valid else None,
    }
    return values


def weather(prefix, enabled=False, present=False, valid=False):
    values = {
        f"{prefix}_enabled": enabled,
        f"{prefix}_present": present,
        f"{prefix}_valid": valid,
    }
    for name in (
        "air_temperature_c",
        "relative_humidity_pct",
        "barometric_pressure_hpa",
        "wind_speed_m_s",
        "wind_direction_deg",
        "rainfall_mm",
        "light_lux",
    ):
        values[f"{prefix}_{name}"] = 1.0 if valid else None
    return values


def valid_status():
    payload = {
        "board_profile": "opta-solinst",
        "site_health": "healthy",
        "rs485_channel_count": 1,
        "display_behavior": "wake_on_demand",
        "display_present": True,
        "display_awake": False,
        "display_refresh_count": 1,
        "display_recovery_count": 0,
        "last_display_refresh_age": "2m 0s",
        "device_id": "opta-test-01",
        "wifi_connected": True,
        "http_server_started": True,
        "uptime_s": 120,
        "clock_valid": True,
        "clock_valid_check": True,
        "clock_now_utc": "2026-08-28T06:00:00Z",
        "ntp_attempted": True,
        "last_ntp_attempt_succeeded": True,
        "last_ntp_attempt_age": "2m 0s",
        "ntp_cooldown_remaining_ms": 21480000,
        "consecutive_ntp_failures": 0,
        "sensor_found": True,
        "modbus_id": 1,
        "sensor_discovery_attempted": True,
        "sensor_discovery_attempt_count": 1,
        "last_sensor_discovery_attempt_succeeded": True,
        "last_sensor_discovery_attempt_age": "2m 0s",
        "sensor_discovery_cooldown_remaining_ms": 0,
        "consecutive_sensor_discovery_failures": 0,
        "cached_probe_valid": True,
        "successful_probe_reads": 4,
        "failed_probe_reads": 0,
        "successful_uploads": 3,
        "failed_uploads": 1,
        "permanent_upload_rejections": 2,
        "permanent_backlog_drops": 1,
        "last_permanent_upload_rejection_status": 409,
        "last_permanent_upload_rejection_error": "HTTP 409: conflict",
        "last_permanent_upload_rejection_utc": "2026-08-28T05:00:00Z",
        "last_permanent_upload_rejected_reading_utc": "2026-08-28T04:00:00Z",
        "backlog_count": 2,
        "dropped_backlog_entries": 0,
        "power_snapshot_initialized": True,
        "power_snapshot_age": "3s",
        "power_poll_interval_ms": 10000,
    }
    payload.update(power("live_battery_output"))
    payload.update(power("live_solar_input"))
    payload.update(weather("live_weather"))
    return payload


def valid_probe():
    payload = {
        "ok": True,
        "timestamp_utc": "2026-08-28T06:00:01Z",
        "water_level_m": 1.2345,
        "temperature_c": 18.5,
        "units": {"level": "m", "temperature": "C"},
    }
    payload.update(power("battery_output"))
    payload.update(power("solar_input"))
    payload.update(weather("weather"))
    return payload


class FakeLogger:
    def __init__(self, responses):
        self.responses = responses
        self.paths = []
        outer = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                outer.paths.append(self.path)
                status, content_type, payload = outer.responses[self.path]
                body = payload if isinstance(payload, bytes) else json.dumps(payload).encode()
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, _format, *_args):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return f"http://127.0.0.1:{self.server.server_port}"

    def __exit__(self, *_args):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()


class HilSmokeTests(unittest.TestCase):
    def test_success_calls_only_status_and_probe(self):
        fake = FakeLogger(
            {
                "/status": (200, "application/json", valid_status()),
                "/probe": (200, "application/json", valid_probe()),
            }
        )
        with fake as base_url:
            device_id = hil_smoke.run_smoke(base_url, "opta-test-01")
        self.assertEqual("opta-test-01", device_id)
        self.assertEqual(["/status", "/probe"], fake.paths)

    def test_expected_device_id_mismatch_stops_before_probe(self):
        fake = FakeLogger({"/status": (200, "application/json", valid_status())})
        with fake as base_url:
            with self.assertRaisesRegex(hil_smoke.SmokeFailure, "device_id mismatch"):
                hil_smoke.run_smoke(base_url, "another-device")
        self.assertEqual(["/status"], fake.paths)

    def test_status_rejects_inconsistent_sensor_state(self):
        payload = valid_status()
        payload["sensor_found"] = False
        with self.assertRaisesRegex(hil_smoke.SmokeFailure, "sensor_found must agree"):
            hil_smoke.validate_status(payload)

    def test_status_allows_clock_snapshot_fields_to_differ(self):
        payload = valid_status()
        payload["clock_valid_check"] = False
        hil_smoke.validate_status(payload)

    def test_status_rejects_server_not_started(self):
        payload = valid_status()
        payload["http_server_started"] = False
        with self.assertRaisesRegex(
            hil_smoke.SmokeFailure, "http_server_started must be true"
        ):
            hil_smoke.validate_status(payload)

    def test_status_rejects_profile_capability_mismatch(self):
        payload = valid_status()
        payload["rs485_channel_count"] = 2
        with self.assertRaisesRegex(
            hil_smoke.SmokeFailure, "does not match board_profile"
        ):
            hil_smoke.validate_status(payload)

    def test_status_accepts_consistent_giga_profile(self):
        payload = valid_status()
        payload.update(
            {
                "board_profile": "giga-site",
                "rs485_channel_count": 2,
                "display_behavior": "persistent_epaper",
            }
        )
        hil_smoke.validate_status(
            payload, expected_board_profile="giga-site"
        )

    def test_status_rejects_missing_configured_giga_display(self):
        payload = valid_status()
        payload.update(
            {
                "board_profile": "giga-site",
                "rs485_channel_count": 2,
                "display_behavior": "persistent_epaper",
                "display_present": False,
            }
        )
        with self.assertRaisesRegex(
            hil_smoke.SmokeFailure, "must report display_present"
        ):
            hil_smoke.validate_status(payload)

    def test_status_rejects_incomplete_permanent_upload_diagnostics(self):
        payload = valid_status()
        payload["last_permanent_upload_rejection_error"] = ""
        with self.assertRaisesRegex(
            hil_smoke.SmokeFailure,
            "permanent rejection diagnostics must be populated",
        ):
            hil_smoke.validate_status(payload)

    def test_status_rejects_impossible_permanent_drop_count(self):
        payload = valid_status()
        payload["permanent_backlog_drops"] = 3
        with self.assertRaisesRegex(
            hil_smoke.SmokeFailure,
            "permanent_backlog_drops cannot exceed",
        ):
            hil_smoke.validate_status(payload)

    def test_probe_rejects_non_finite_measurement(self):
        payload = valid_probe()
        payload["water_level_m"] = float("nan")
        with self.assertRaisesRegex(hil_smoke.SmokeFailure, "water_level_m must be finite"):
            hil_smoke.validate_probe(payload)

    def test_probe_rejects_invalid_power_shape(self):
        payload = valid_probe()
        payload["battery_output_monitor_valid"] = False
        with self.assertRaisesRegex(
            hil_smoke.SmokeFailure, "battery_output_voltage_v must be null"
        ):
            hil_smoke.validate_probe(payload)

    def test_http_error_is_actionable(self):
        fake = FakeLogger(
            {"/status": (503, "application/json", {"error": "not ready"})}
        )
        with fake as base_url:
            with self.assertRaisesRegex(hil_smoke.SmokeFailure, "HTTP 503.*not ready"):
                hil_smoke.run_smoke(base_url)

    def test_malformed_json_is_rejected(self):
        fake = FakeLogger({"/status": (200, "application/json", b"{not-json")})
        with fake as base_url:
            with self.assertRaisesRegex(hil_smoke.SmokeFailure, "invalid JSON"):
                hil_smoke.run_smoke(base_url)

    def test_non_json_content_type_is_rejected(self):
        fake = FakeLogger({"/status": (200, "text/html", valid_status())})
        with fake as base_url:
            with self.assertRaisesRegex(hil_smoke.SmokeFailure, "application/json"):
                hil_smoke.run_smoke(base_url)


if __name__ == "__main__":
    unittest.main()
