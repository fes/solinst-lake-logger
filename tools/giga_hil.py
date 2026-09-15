#!/usr/bin/env python3
"""Automated USB-serial HIL runner for the Giga diagnostic firmware."""

import argparse
import dataclasses
import json
import sys
import time


class HilFailure(Exception):
    """An actionable Giga HIL validation failure."""


@dataclasses.dataclass
class TestResult:
    name: str
    status: str
    detail: str
    duration_ms: int


class SerialTransport:
    def __init__(self, port, baud=115200):
        try:
            import serial
        except ImportError as exc:
            raise HilFailure(
                "pyserial is required for device testing; install it with "
                "'python3 -m pip install pyserial'"
            ) from exc
        try:
            self._serial = serial.Serial(port, baudrate=baud, timeout=0.1)
        except serial.SerialException as exc:
            raise HilFailure(f"could not open serial port {port!r}: {exc}") from exc
        self._serial.reset_input_buffer()

    def close(self):
        self._serial.close()

    def request(self, command, timeout):
        self._serial.write((command + "\n").encode("ascii"))
        self._serial.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = self._serial.readline()
            if not raw:
                continue
            try:
                payload = json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError):
                continue
            if not isinstance(payload, dict):
                continue
            if payload.get("command") == command.split(" ", 1)[0]:
                return payload
        raise HilFailure(f"{command}: timed out after {timeout:.1f}s")


def _require(payload, key, expected_type):
    if key not in payload:
        raise HilFailure(f"response missing {key!r}")
    value = payload[key]
    if expected_type is int:
        valid = type(value) is int
    else:
        valid = isinstance(value, expected_type)
    if not valid:
        raise HilFailure(
            f"response field {key!r} must be {expected_type.__name__}, "
            f"got {type(value).__name__}"
        )
    return value


def _successful(payload, command):
    if payload.get("hil_protocol") != 1:
        raise HilFailure(f"{command}: unsupported or missing HIL protocol version")
    if payload.get("command") != command:
        raise HilFailure(f"{command}: response command does not match request")
    if payload.get("ok") is not True:
        raise HilFailure(f"{command}: {payload.get('error', 'device reported failure')}")
    return payload


def validate_hello(payload):
    _successful(payload, "HELLO")
    device = _require(payload, "device", str)
    if not device.strip():
        raise HilFailure("HELLO: device name must not be empty")
    if payload.get("board") != "giga_r1_m7":
        raise HilFailure("HELLO: firmware is not running on the Giga M7 target")
    capabilities = _require(payload, "capabilities", dict)
    for name in (
        "rs485_channel_1",
        "rs485_channel_2",
        "rs485_bridge",
    ):
        if type(capabilities.get(name)) is not bool:
            raise HilFailure(f"HELLO: capability {name!r} must be boolean")
    if (
        capabilities["rs485_channel_1"] or capabilities["rs485_channel_2"]
    ) and not capabilities["rs485_bridge"]:
        raise HilFailure(
            "HELLO: SC16IS752 bridge is unavailable while an RS-485 channel is enabled"
        )
    return device, capabilities


def validate_status(payload):
    _successful(payload, "STATUS")
    uptime = _require(payload, "uptime_ms", int)
    if uptime < 0:
        raise HilFailure("STATUS: uptime_ms must not be negative")
    irq = payload.get("rs485_irq_active")
    if irq is not None and type(irq) is not bool:
        raise HilFailure("STATUS: rs485_irq_active must be boolean or null")


def validate_rs485_read(payload, channel, quantity):
    _successful(payload, "RS485_READ")
    if payload.get("channel") != channel:
        raise HilFailure("RS485_READ: response channel does not match request")
    elapsed = _require(payload, "elapsed_ms", int)
    if elapsed < 0:
        raise HilFailure("RS485_READ: elapsed_ms must not be negative")
    raw_hex = _require(payload, "raw_hex", str)
    try:
        bytes.fromhex(raw_hex)
    except ValueError as exc:
        raise HilFailure("RS485_READ: raw_hex is not valid hexadecimal") from exc
    registers = _require(payload, "registers", list)
    if len(registers) != quantity:
        raise HilFailure(
            f"RS485_READ: expected {quantity} registers, got {len(registers)}"
        )
    if any(type(value) is not int or value < 0 or value > 65535 for value in registers):
        raise HilFailure("RS485_READ: register values must be unsigned 16-bit integers")
    return registers


def _record(results, name, operation):
    started = time.monotonic()
    try:
        detail = operation()
    except HilFailure as exc:
        status = "failed"
        detail = str(exc)
    else:
        status = "passed"
        detail = detail or "ok"
    results.append(
        TestResult(
            name=name,
            status=status,
            detail=detail,
            duration_ms=round((time.monotonic() - started) * 1000),
        )
    )


def _skip(results, name, detail):
    results.append(TestResult(name, "skipped", detail, 0))


def _read_command(channel, baud, framing, slave, function, start, quantity, timeout_ms):
    return (
        f"RS485_READ {channel} {baud} {framing} {slave} {function} "
        f"{start} {quantity} {timeout_ms}"
    )


def run_hil(
    transport,
    suite="smoke",
    *,
    command_timeout=5.0,
    solinst_channel=1,
    solinst_baud=19200,
    solinst_slave=1,
    weather_channel=2,
    weather_baud=19200,
    weather_slave=2,
):
    if suite not in ("smoke", "sensors"):
        raise ValueError(f"unknown suite {suite!r}")

    results = []
    context = {}

    def hello():
        payload = transport.request("HELLO", command_timeout)
        device, capabilities = validate_hello(payload)
        context["device"] = device
        context["capabilities"] = capabilities
        return device

    _record(results, "firmware handshake", hello)
    if results[-1].status == "failed":
        return {"device": None, "suite": suite, "results": results}

    def status():
        validate_status(transport.request("STATUS", command_timeout))
        return "runtime status is valid"

    _record(results, "runtime status", status)
    capabilities = context["capabilities"]

    if suite == "sensors":
        sensor_specs = (
            (
                "Solinst channel",
                "rs485_channel_1" if solinst_channel == 1 else "rs485_channel_2",
                _read_command(
                    solinst_channel,
                    solinst_baud,
                    "8E1",
                    solinst_slave,
                    4,
                    0,
                    2,
                    2000,
                ),
                solinst_channel,
                2,
            ),
            (
                "weather channel",
                "rs485_channel_1" if weather_channel == 1 else "rs485_channel_2",
                _read_command(
                    weather_channel,
                    weather_baud,
                    "8N1",
                    weather_slave,
                    3,
                    0x01F4,
                    1,
                    2000,
                ),
                weather_channel,
                1,
            ),
        )
        for name, capability, command, channel, quantity in sensor_specs:
            if not capabilities[capability]:
                _skip(results, name, f"{capability} disabled in hil_config.h")
                continue

            def read(command=command, channel=channel, quantity=quantity):
                registers = validate_rs485_read(
                    transport.request(command, command_timeout), channel, quantity
                )
                return f"registers={registers}"

            _record(results, name, read)

    return {"device": context["device"], "suite": suite, "results": results}


def report_dict(report):
    return {
        "device": report["device"],
        "suite": report["suite"],
        "passed": sum(result.status == "passed" for result in report["results"]),
        "failed": sum(result.status == "failed" for result in report["results"]),
        "skipped": sum(result.status == "skipped" for result in report["results"]),
        "results": [dataclasses.asdict(result) for result in report["results"]],
    }


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Run automated tests against the Giga USB-serial HIL firmware."
    )
    parser.add_argument("port", help="USB serial port, for example /dev/cu.usbmodem101")
    parser.add_argument(
        "--suite",
        choices=("smoke", "sensors"),
        default="smoke",
        help="smoke is passive; sensors performs Modbus reads",
    )
    parser.add_argument("--command-timeout", type=float, default=5.0)
    parser.add_argument("--solinst-channel", type=int, choices=(1, 2), default=1)
    parser.add_argument("--solinst-baud", type=int, default=19200)
    parser.add_argument("--solinst-slave", type=int, default=1)
    parser.add_argument("--weather-channel", type=int, choices=(1, 2), default=2)
    parser.add_argument("--weather-baud", type=int, default=19200)
    parser.add_argument("--weather-slave", type=int, default=2)
    parser.add_argument("--json-output", help="write the complete result report to a file")
    args = parser.parse_args(argv)

    if args.command_timeout <= 0:
        parser.error("--command-timeout must be greater than zero")
    transport = None
    try:
        transport = SerialTransport(args.port)
        report = run_hil(
            transport,
            args.suite,
            command_timeout=args.command_timeout,
            solinst_channel=args.solinst_channel,
            solinst_baud=args.solinst_baud,
            solinst_slave=args.solinst_slave,
            weather_channel=args.weather_channel,
            weather_baud=args.weather_baud,
            weather_slave=args.weather_slave,
        )
    except HilFailure as exc:
        print(f"Giga HIL FAILED: {exc}", file=sys.stderr)
        return 1
    finally:
        if transport is not None:
            transport.close()

    output = report_dict(report)
    if args.json_output:
        with open(args.json_output, "w", encoding="utf-8") as handle:
            json.dump(output, handle, indent=2)
            handle.write("\n")

    for result in report["results"]:
        print(f"{result.status.upper():7} {result.name}: {result.detail}")
    print(
        f"Giga HIL: {output['passed']} passed, {output['failed']} failed, "
        f"{output['skipped']} skipped"
    )
    return 1 if output["failed"] else 0


if __name__ == "__main__":
    sys.exit(main())
