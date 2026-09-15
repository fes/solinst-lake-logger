import unittest

from tools import giga_hil


def hello(channel1=True, channel2=True):
    return {
        "hil_protocol": 1,
        "command": "HELLO",
        "ok": True,
        "device": "giga-bench-01",
        "board": "giga_r1_m7",
        "capabilities": {
            "rs485_channel_1": channel1,
            "rs485_channel_2": channel2,
            "rs485_bridge": channel1 or channel2,
        },
    }


def status():
    return {
        "hil_protocol": 1,
        "command": "STATUS",
        "ok": True,
        "uptime_ms": 1234,
        "rs485_irq_active": False,
    }


def rs485(channel, registers, raw_hex="0104020001B930"):
    return {
        "hil_protocol": 1,
        "command": "RS485_READ",
        "ok": True,
        "channel": channel,
        "elapsed_ms": 12,
        "raw_hex": raw_hex,
        "frame_offset": 0,
        "registers": registers,
    }


class FakeTransport:
    def __init__(self, responses):
        self.responses = responses
        self.commands = []

    def request(self, command, _timeout):
        self.commands.append(command)
        if command not in self.responses:
            raise giga_hil.HilFailure(f"no response registered for {command!r}")
        response = self.responses[command]
        if isinstance(response, Exception):
            raise response
        return response


class GigaHilTests(unittest.TestCase):
    def test_smoke_is_passive_and_validates_identity(self):
        transport = FakeTransport({"HELLO": hello(), "STATUS": status()})
        report = giga_hil.run_hil(transport)
        output = giga_hil.report_dict(report)
        self.assertEqual("giga-bench-01", output["device"])
        self.assertEqual(2, output["passed"])
        self.assertEqual(0, output["failed"])
        self.assertEqual(["HELLO", "STATUS"], transport.commands)

    def test_sensor_suite_uses_expected_channels_and_framing(self):
        solinst = "RS485_READ 1 19200 8E1 1 4 0 2 2000"
        weather = "RS485_READ 2 19200 8N1 2 3 500 1 2000"
        transport = FakeTransport(
            {
                "HELLO": hello(),
                "STATUS": status(),
                solinst: rs485(1, [0x0102, 3]),
                weather: rs485(2, [42]),
            }
        )
        output = giga_hil.report_dict(
            giga_hil.run_hil(transport, suite="sensors")
        )
        self.assertEqual(4, output["passed"])
        self.assertEqual(0, output["failed"])
        self.assertEqual(["HELLO", "STATUS", solinst, weather], transport.commands)

    def test_disabled_sensor_channel_is_skipped_without_transmitting(self):
        weather = "RS485_READ 2 19200 8N1 2 3 500 1 2000"
        transport = FakeTransport(
            {
                "HELLO": hello(channel1=False),
                "STATUS": status(),
                weather: rs485(2, [42]),
            }
        )
        output = giga_hil.report_dict(
            giga_hil.run_hil(transport, suite="sensors")
        )
        self.assertEqual(1, output["skipped"])
        self.assertNotIn("RS485_READ 1 19200 8E1 1 4 0 2 2000", transport.commands)

    def test_device_failure_becomes_failed_test_with_diagnostic(self):
        solinst = "RS485_READ 1 19200 8E1 1 4 0 2 2000"
        weather = "RS485_READ 2 19200 8N1 2 3 500 1 2000"
        failure = {
            "hil_protocol": 1,
            "command": "RS485_READ",
            "ok": False,
            "error": "no valid Modbus response",
        }
        transport = FakeTransport(
            {
                "HELLO": hello(),
                "STATUS": status(),
                solinst: failure,
                weather: rs485(2, [42]),
            }
        )
        report = giga_hil.run_hil(transport, suite="sensors")
        output = giga_hil.report_dict(report)
        self.assertEqual(1, output["failed"])
        self.assertIn(
            "no valid Modbus response",
            next(result.detail for result in report["results"] if result.status == "failed"),
        )

    def test_invalid_register_shape_is_rejected(self):
        payload = rs485(1, [1])
        with self.assertRaisesRegex(giga_hil.HilFailure, "expected 2 registers"):
            giga_hil.validate_rs485_read(payload, 1, 2)

    def test_invalid_raw_hex_is_rejected(self):
        payload = rs485(1, [1], raw_hex="XYZ")
        with self.assertRaisesRegex(giga_hil.HilFailure, "valid hexadecimal"):
            giga_hil.validate_rs485_read(payload, 1, 1)

    def test_handshake_failure_stops_without_other_commands(self):
        transport = FakeTransport(
            {
                "HELLO": {
                    "hil_protocol": 2,
                    "command": "HELLO",
                    "ok": True,
                }
            }
        )
        output = giga_hil.report_dict(giga_hil.run_hil(transport, suite="sensors"))
        self.assertEqual(1, output["failed"])
        self.assertEqual(["HELLO"], transport.commands)

    def test_handshake_rejects_non_giga_firmware(self):
        payload = hello()
        payload["board"] = "opta"
        with self.assertRaisesRegex(giga_hil.HilFailure, "Giga M7"):
            giga_hil.validate_hello(payload)

    def test_handshake_rejects_enabled_channel_without_bridge(self):
        payload = hello(channel1=True)
        payload["capabilities"]["rs485_bridge"] = False
        with self.assertRaisesRegex(giga_hil.HilFailure, "bridge is unavailable"):
            giga_hil.validate_hello(payload)

    def test_transport_timeout_becomes_failed_test(self):
        transport = FakeTransport(
            {
                "HELLO": hello(),
                "STATUS": giga_hil.HilFailure("STATUS: timed out after 0.1s"),
            }
        )
        output = giga_hil.report_dict(giga_hil.run_hil(transport))
        self.assertEqual(1, output["failed"])
        self.assertEqual(1, output["passed"])

if __name__ == "__main__":
    unittest.main()
