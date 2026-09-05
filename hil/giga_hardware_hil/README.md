# Giga hardware-in-the-loop firmware

This standalone Arduino GIGA R1 M7 image exposes bounded diagnostic commands
over the USB serial port. It does not contain Wi-Fi credentials, upload data,
format storage, or change Modbus registers. E-paper pixel output is limited to
the confirmation-gated test-pattern command.

Copy `include/hil_config.example.h` to `include/hil_config.h` and replace its
values from the reviewed wiring diagram. The Waveshare SC16IS752 on SPI1
provides channel 1 and channel 2. On Giga, those are D0/D1 and D19/D18 RX/TX respectively. DE and
RE may use the same pin; use `-1` for an automatically controlled transceiver
signal. All GPIO is 3.3 V only. Do not guess pin values.

Build and upload:

```sh
pio run -d hil/giga_hardware_hil -e giga-hil
pio run -d hil/giga_hardware_hil -e giga-hil -t upload
```

The `giga-hil-placeholder` environment is a compile-only CI target with both
RS-485 channels and e-paper disabled. Never use it as wiring evidence.

Supported commands:

- `HELLO`
- `STATUS`
- `RS485_READ channel baud 8N1|8E1 slave function start quantity timeout_ms`
- `RS485_LOOPBACK channel`
- `EPAPER_WAIT_IDLE timeout_ms`
- `EPAPER_RESET CONFIRM`
- `EPAPER_PATTERN CONFIRM`
- `EPAPER_PARTIAL_PATTERN CONFIRM` (after a full pattern)

Only Modbus read functions `0x03` and `0x04` are accepted. E-paper reset and
pattern output require the literal confirmation token. Pattern output uses the
same GxEPD2 GDEQ0426T82/SSD1677 driver as production firmware.
