# Logger architecture

The repository is evolving into two related firmware targets that share an
application model but have different hardware responsibilities.

| Profile | Current status | Role | RS-485 | Display |
| --- | --- | --- | --- | --- |
| `opta-solinst` | supported/default | Water-level logger | Opta RS-485 to Solinst 301 | SSD1309 OLED, button-wake and timed sleep |
| `giga-site` | planned | Full site logger | Isolated channel 1 to Solinst; isolated channel 2 to weather station | Waveshare 4.26-inch e-paper, persistent last image |

`board_profile.h` records these capabilities. Only `opta-solinst` is selectable
today. The Giga declaration is a design target, not an incomplete build option.

## Boundaries

The application should own policy and data flow:

- logging and weather sampling schedules;
- `ProbeReading`, sensor identity, power, and weather data models;
- upload payloads, retry cooldown, and backlog;
- NTP time, local HTTP status, and diagnostics.

Hardware-facing code should sit behind four boundaries:

1. **Platform** — board startup, input, status indicators, and board-specific
   services. `platform_opta.ino` currently delegates to the existing Opta UI.
2. **RS-485 channel** — UART selection, framing, direction control, buffer
   handling, and transaction timing. The existing implementation remains Opta
   specific. The Giga implementation will expose two independent channels.
3. **Sensor driver** — register map, Modbus function, decoding, validation, and
   retry semantics. Solinst and weather logic should not choose physical UARTs.
4. **Display** — render a status snapshot and apply target-specific refresh and
   power policy. The Opta OLED wakes temporarily; e-paper keeps its last image
   without requiring periodic refresh.

`runtime_boundaries.h` introduces small platform, display, and auxiliary-sensor
adapters now. They wrap the existing functions so the first separation step does
not alter the deployed Opta behavior.

## Current Opta target

The supported Opta firmware is intentionally Solinst-only:

- the Solinst 301 remains on the integrated Opta RS-485 interface at its current
  baud, parity, timing, and fixed Modbus ID;
- INA228 monitoring, Wi-Fi, HTTP, upload, backlog, LEDs, button, and OLED retain
  their existing behavior;
- weather polling remains disabled by the active board profile;
- the weather driver and standalone diagnostic/configuration sketches stay in
  the repository as reference material for Giga bring-up.

This avoids making the known-working water-level logger depend on the shared-bus
framing, addressing, or electrical behavior of the weather station.

## Planned Giga site logger

The target hardware is Arduino GIGA R1 WiFi (or the confirmed successor board),
a Waveshare two-channel isolated RS-485 expansion board, the existing INA228
monitors, a Waveshare 4.26-inch e-paper HAT, and a Victron SmartSolar MPPT
75/15.

Planned bus ownership:

```text
Giga site logger
  RS-485 channel 1  -> Solinst 301 (19200, 8E1)
  RS-485 channel 2  -> DFRobot weather station (its supported framing/address)
  I2C               -> INA228 battery and solar monitors
  display interface -> Waveshare 4.26-inch e-paper HAT
```

Separate RS-485 channels are a core requirement. They remove the need to switch
framing on a shared bus and isolate termination, addressing, timing, and fault
diagnosis between the two instruments.

The e-paper adapter should:

- render from the shared status/reading model, not read sensors directly;
- update on meaningful state changes and a conservative maximum refresh rate;
- leave the last complete image visible between updates;
- expose busy/timeout/failure state without blocking logging indefinitely;
- support full-refresh maintenance if the selected panel requires it.

The first Giga implementation should keep Victron integration limited to the
existing INA228 electrical measurements. Direct Victron telemetry (for example,
VE.Direct) should be a separate optional sensor adapter once its electrical
interface and desired fields are confirmed.

## Migration sequence

1. Keep `opta-solinst` as the default and verify it after every boundary change.
2. Introduce an RS-485 channel contract and move the existing Opta transaction
   mechanics behind channel 1 without changing the Solinst decoder.
3. Add a separate Giga sketch/build target and the dual-channel HAT pin map.
4. Bind Solinst to Giga channel 1 and verify it before adding weather.
5. Bind weather to Giga channel 2 and validate each station register independently.
6. Add the e-paper display adapter using cached application state.
7. Add board-specific documentation and bench/field acceptance tests.

## Acceptance rules

- An Opta build must not initialize weather RS-485 hardware or poll the station.
- A failed optional sensor or display must not discard a valid Solinst reading.
- Display work must not block the measurement/upload loop indefinitely.
- Sensor drivers must not depend on a board-specific UART or display library.
- Giga RS-485 channels must be testable independently before combined operation.
- Secrets and site-specific calibration stay outside board profiles.
