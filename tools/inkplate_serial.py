#!/usr/bin/env python3
"""Send checksummed maintenance commands to the Inkplate 6MOTION display."""

import argparse
import dataclasses
import time

PROTOCOL_VERSION = 1
MAX_FRAME_LENGTH = 1536
COMMANDS = ("status", "help", "refresh", "clear", "pause", "resume", "reboot", "sleep")


class ProtocolError(Exception):
    """The display returned or received an invalid protocol frame."""


@dataclasses.dataclass(frozen=True)
class Frame:
    version: int
    sequence: int
    frame_type: str
    payload: str


def crc16_ccitt(data):
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(sequence, frame_type, payload):
    if not 0 < sequence <= 0xFFFFFFFF:
        raise ProtocolError("sequence must be between 1 and 4294967295")
    if "|" in frame_type or any(character in payload for character in "\r\n|*"):
        raise ProtocolError("frame contains a reserved delimiter")
    body = f"{PROTOCOL_VERSION}|{sequence}|{frame_type}|{payload}"
    checksum = crc16_ccitt(body.encode("ascii"))
    encoded = f"@{body}*{checksum:04X}\n"
    if len(encoded) - 1 > MAX_FRAME_LENGTH:
        raise ProtocolError("frame exceeds protocol limit")
    return encoded


def decode_frame(line):
    stripped = line.rstrip("\r\n")
    if len(stripped) > MAX_FRAME_LENGTH:
        raise ProtocolError("frame exceeds protocol limit")
    if not stripped.startswith("@") or "*" not in stripped:
        raise ProtocolError("invalid frame wrapper")
    body, checksum_text = stripped[1:].rsplit("*", 1)
    if len(checksum_text) != 4:
        raise ProtocolError("invalid checksum width")
    try:
        expected = int(checksum_text, 16)
    except ValueError as exc:
        raise ProtocolError("invalid checksum") from exc
    if crc16_ccitt(body.encode("ascii")) != expected:
        raise ProtocolError("checksum mismatch")
    fields = body.split("|", 3)
    if len(fields) != 4:
        raise ProtocolError("invalid frame fields")
    try:
        version = int(fields[0])
        sequence = int(fields[1])
    except ValueError as exc:
        raise ProtocolError("invalid numeric field") from exc
    if version != PROTOCOL_VERSION:
        raise ProtocolError(f"unsupported protocol version {version}")
    return Frame(version, sequence, fields[2], fields[3])


def request(port, baud, sequence, command, timeout, retry_stale=False):
    try:
        import serial
    except ImportError as exc:
        raise ProtocolError(
            "pyserial is required; install it with 'python3 -m pip install pyserial'"
        ) from exc

    try:
        with serial.Serial(port, baudrate=baud, timeout=0.1) as connection:
            connection.reset_input_buffer()
            deadline = time.monotonic() + timeout
            active_sequence = sequence
            connection.write(
                encode_frame(active_sequence, "COMMAND", command).encode("ascii")
            )
            connection.flush()
            while time.monotonic() < deadline:
                raw = connection.readline()
                if not raw:
                    continue
                try:
                    response = decode_frame(raw.decode("ascii"))
                except (UnicodeDecodeError, ProtocolError):
                    continue
                if response.sequence != active_sequence:
                    continue
                if (
                    retry_stale
                    and response.frame_type == "ERROR"
                    and response.payload == "reason=stale_sequence"
                ):
                    while int(time.time()) <= active_sequence:
                        if time.monotonic() >= deadline:
                            raise ProtocolError(f"timed out after {timeout:.1f}s")
                        time.sleep(0.01)
                    active_sequence = int(time.time())
                    connection.write(
                        encode_frame(
                            active_sequence, "COMMAND", command
                        ).encode("ascii")
                    )
                    connection.flush()
                    retry_stale = False
                    continue
                if response.frame_type in ("ACK", "ERROR"):
                    return response
    except serial.SerialException as exc:
        raise ProtocolError(f"could not use serial port {port!r}: {exc}") from exc
    raise ProtocolError(f"timed out after {timeout:.1f}s")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=COMMANDS)
    parser.add_argument("--port", help="USB or device serial port")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--sequence",
        type=int,
        default=None,
        help="positive request sequence; defaults to current Unix time",
    )
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument(
        "--frame-only",
        action="store_true",
        help="print the encoded command without opening a serial port",
    )
    args = parser.parse_args()
    sequence = args.sequence if args.sequence is not None else int(time.time())

    if args.frame_only:
        print(encode_frame(sequence, "COMMAND", args.command), end="")
        return
    if not args.port:
        parser.error("--port is required unless --frame-only is used")
    try:
        response = request(
            args.port,
            args.baud,
            sequence,
            args.command,
            args.timeout,
            retry_stale=args.sequence is None,
        )
    except ProtocolError as exc:
        parser.exit(1, f"error: {exc}\n")
    print(f"{response.frame_type}: {response.payload}")
    if response.frame_type == "ERROR":
        raise SystemExit(1)


if __name__ == "__main__":
    main()
