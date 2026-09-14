# Inkplate 6MOTION serial display

This standalone Arduino sketch turns an Inkplate 6MOTION into a wired display
peripheral for the lake logger. The Inkplate owns layout and refresh behavior;
the GIGA sends semantic site snapshots rather than pixel or drawing commands.
The initial dashboard uses the native 1024x758 landscape canvas for water,
weather, power, logger health, network state, and serial diagnostics.

The sketch targets the official Soldered STM32 board package and cannot
currently be built with the logger's PlatformIO environments.

## Install and upload

1. Install Arduino IDE 2.x and STM32CubeProgrammer.
2. Add this Boards Manager URL:
   `https://github.com/SolderedElectronics/Inkplate-Board-Definitions-for-Arduino-IDE/raw/refs/heads/main/package_Inkplate_Boards_index.json`
3. Install **Inkplate MOTION Boards** and **Inkplate Motion Library**.
4. Select **Soldered Inkplate 6 MOTION**.
5. Open `inkplate_6motion_display.ino`, press the Inkplate PROGRAMMING button,
   and upload over USB-C.

The sketch programs the STM32H743. Do not replace the firmware on the onboard
ESP32-C3 coprocessor.

## Wiring

The dedicated device port is USART3 at 115200 baud, 8N1:

| GIGA | Inkplate 6MOTION | Purpose |
|---|---|---|
| D1 / `Serial1` TX | PB11 | Inkplate receive |
| D0 / `Serial1` RX | PB10 | Inkplate transmit |
| GND | GND | Common reference |

Both boards use 3.3 V signaling. Cross TX and RX, connect ground, and never
apply 5 V to either UART input. The USB serial port accepts the same protocol
for bench setup without occupying the device UART.

## Framing

Every request and response is one ASCII line:

```text
@<version>|<sequence>|<type>|<payload>*<crc16>\n
```

- Version is currently `1`.
- Sequence is a positive unsigned decimal integer that must increase after
  boot. Zero is reserved for errors that cannot be associated with a request.
- Request types are `SNAPSHOT` and `COMMAND`.
- Responses use `ACK` or `ERROR`.
- CRC is four hexadecimal digits containing CRC-16/CCITT-FALSE over the text
  between `@` and `*` (initial value `0xFFFF`, polynomial `0x1021`).
- A frame is limited to 1536 bytes and a payload to 1400 bytes.
- Snapshot fields are `key=value` pairs separated by semicolons.
- Unknown snapshot fields are ignored for forward compatibility. Duplicate or
  malformed known fields reject the complete snapshot; updates are atomic.
- Communication is stop-and-wait: send one frame, then wait for its `ACK` or
  `ERROR` before sending another. Rendering may block while the e-paper updates,
  and the acknowledgement is deliberately sent only after that update.
- Do not drive the device UART and USB maintenance port concurrently. They have
  independent sequence spaces, but simultaneous senders can overrun a hardware
  UART while the display is refreshing.

Required snapshot identity fields are `device`, `timestamp`, and `health`.
`health` is one of `HEALTHY`, `DEGRADED`, or `CRITICAL`. Supported telemetry
fields are defined in `inkplate_protocol.h`.

Example body before adding its CRC:

```text
1|42|SNAPSHOT|device=silverlake-412-streeter;timestamp=2026-09-14T19:00:00Z;health=HEALTHY;clock=1;wifi=1;rssi=-54;ip=10.2.12.247;sensor_found=1;modbus_id=1;serial=2216073;water_valid=1;water_level_m=0.440;water_temp_c=16.84;probe_age_s=20;weather_enabled=1;weather_present=1;weather_valid=1;air_temp_c=15.4;humidity_pct=80;pressure_hpa=1002;wind_m_s=0.7;wind_deg=180;rain_valid=1;rain_mm=0.2;battery_valid=1;battery_v=13.39;battery_pct=99;battery_extrema_valid=1;battery_min_v=13.10;battery_max_v=13.58;solar_valid=1;solar_v=15.18;solar_a=0.28;solar_w=4.24;solar_charging=1;backlog=0;upload_failures=0;upload_age_s=20
```

The GIGA probes the Inkplate at startup, falls back to its SPI e-paper panel if
the UART probe fails, and periodically retries detection while fallback is
active. Its HTTP API exposes the same maintenance commands at
`/display/<command>` using POST (with GET reserved for `/display/status`) and
reports the selected backend and link diagnostics from `/status`.

## Utility commands

Commands use a `COMMAND` frame with one exact lowercase payload:

| Command | Behavior |
|---|---|
| `status` | Return state, counters, sequence, and uptime |
| `help` | Return the supported command names |
| `refresh` | Perform a full refresh of the current dashboard |
| `clear` | Clear the panel and pause automatic rendering |
| `pause` | Accept snapshots without updating the panel |
| `resume` | Resume rendering and perform a full refresh |
| `reboot` | Acknowledge, flush serial output, then reset the STM32 |
| `sleep` | Acknowledge, disable peripherals, then enter STM32 standby |

`sleep` is intentionally terminal for the UART session. Deep standby disables
the UART, so the Inkplate must be woken with its WAKE button, reset, or a power
cycle. Use `pause` when the display must remain reachable over serial.

Use the host utility to frame commands and verify responses:

```bash
python3 tools/inkplate_serial.py status --port /dev/cu.usbserial-XXXX
python3 tools/inkplate_serial.py refresh --port /dev/cu.usbserial-XXXX
```

It uses `pyserial`, like the GIGA HIL utility. With `--frame-only`, it prints a
checksummed command without opening a port and requires no external package.
By default, it uses Unix time as the sequence and automatically retries once
after the next second if another invocation already used that sequence.

The renderer uses black-and-white mode, partial updates for snapshots, and an
automatic full refresh after 40 partial updates. It never leaves the display
power supply enabled between ordinary updates.
