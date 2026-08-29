# Solinst Lake Logger

A dual-target field logger for **Arduino Opta WiFi** and **Arduino GIGA R1
WiFi** that:

- reads **water level** and **temperature** from a **Solinst 301** over **Modbus RTU / RS-485**
- timestamps readings with NTP-synchronized UTC time
- exposes local HTTP endpoints for health and diagnostics
- uploads readings to either the existing **Google Apps Script** web app or a
  new **fesLabs ingest endpoint**, selected at compile time
- reads two **INA228** I2C power monitors:
  - **battery output** monitor at **0x40**
  - **solar input** monitor at **0x41**
- uses the Opta status LEDs for field diagnostics
- supports a **2.42 inch SSD1309 I2C OLED** on Opta
- supports the **Waveshare 4.26-inch e-Paper HAT, SKU 26376** on Giga
- reads a **DFRobot SEN0657 7-in-1 weather station** on the shared Opta bus
  when enabled, or on Giga's independent second RS-485 channel

The logger currently uploads:

- timestamp
- Solinst water level and temperature
- Solinst identity metadata (Modbus ID, serial number, firmware)
- INA228 battery output voltage/current/power
- INA228 solar input voltage/current/power
- an approximate battery charge percentage
- a boolean indicating whether solar appears to be charging the battery
- optional weather values: wind speed/direction, air temperature, relative humidity, barometric pressure, light, and rainfall accumulation

---

## Repository layout

- `platformio.ini` - Opta and Giga production/placeholder firmware plus
  Opta/Giga native test environments
- `src/main.cpp` - main setup/loop
- `src/app_state.cpp` - single owner for runtime state and hardware objects
- `include/config.h` - compile-time defaults, data models, state declarations, and service contracts
- `include/board_profile.h` - build-selected Opta and Giga capabilities
- `include/giga_board_config.h` - reviewed Giga transceiver pin configuration
- `include/rs485_channel.h` / `src/rs485_*.cpp` - board-specific RS-485 channels
- `include/runtime_boundaries.h` / `src/platform_*.cpp` - platform, display,
  and auxiliary-sensor adapters
- `secrets_example.h` - tracked template for local secrets
- `src/config_file.cpp` - compile-time config initialization and summary output
- `src/` - logger services for Modbus, weather, power, display, HTTP, upload, and time sync
- `lib/logger_core/` - hardware-independent queue, Modbus, battery, and retry policies
- `test/test_logger_core/` - native unit tests for the shared core
- `hil/giga_hardware_hil/` - standalone Giga M7 dual-RS-485/e-paper HIL firmware
- `tools/giga_hil.py` - USB-serial Giga HIL automation runner
- `r4_weather_station_probe/r4_weather_station_probe.ino` - standalone Uno R4
  WiFi probe for the RS232/RS485 Shield V1
- `opta_weather_bus_diagnostic/opta_weather_bus_diagnostic.ino` and
  `r4_weather_bus_diagnostic/r4_weather_bus_diagnostic.ino` - paired master and
  passive-monitor firmware for isolating the Opta's RS-485 transmit/receive paths

The paired diagnostics print timestamps, raw TX/RX or monitored bus frames,
decoded Modbus requests/responses, CRC results, Modbus exceptions, response
timeouts, monitor heartbeats, and receive-buffer overflow events.

In master mode, either diagnostic alternates wind-speed requests between the
factory SEN0657 endpoint (ID 1 at 4800 baud, 8N1) and the provisioned endpoint
(ID 2 at 19200 baud, 8N1). In monitor mode, set `MONITOR_PROFILE_INDEX` to `0`
or `1` before upload because a UART can listen to only one baud rate at a time.
- `google_apps_script.gs` - Google Apps Script endpoint for Sheets logging

---

## Hardware assumptions

The deployed Opta profile assumes:

- **Arduino Opta WiFi**
- **Solinst 301** wired for **Modbus RTU** over the Opta RS-485 interface
- **2x INA228** boards on I2C
  - battery output monitor at `0x40`
  - solar input monitor at `0x41`
- **2.42 inch SSD1309 128x64 I2C OLED** display
- **12 V LiFePO4** battery system with solar charge controller

The Giga profile assumes:

- **Arduino GIGA R1 WiFi**, M7 core, running the logger single-core
- Waveshare 2-CH RS485 HAT with its SC16IS752 dual UART on `SPI1`: D5 CS,
  D4 IRQ, D3 channel 1 enable, D2 channel 2 enable, D12 MISO, D11 MOSI,
  and D13 SCK
- Solinst 301 on SC16IS752 channel 1, 19200 baud, 8E1
- DFRobot SEN0657 on SC16IS752 channel 2, address 2, 19200 baud, 8N1
- both channel mode switches set to Half-auto/manual (positions 3 and 4 ON,
  positions 1 and 2 OFF); EN1/EN2 are HIGH for transmit and idle LOW for
  receive
- 3.3 V UART/GPIO signaling; Giga GPIO must never receive 5 V
- the same INA228 addresses and current-path orientation as Opta
- Waveshare SKU 26376 e-paper on `SPI1`: D10 CS, D9 DC, D8 RST, D7 BUSY,
  D6 PWR, D11 MOSI, and D13 SCK

SKU 26376 uses the black/white `GDEQ0426T82`/SSD1677 panel and GxEPD2's
`GxEPD2_426_GDEQ0426T82` driver. The similarly sized Waveshare four-color
variant is incompatible. The dashboard uses a 40-row/4 KB paged framebuffer,
15-minute partial updates, a daily full refresh, bounded BUSY waits, and powers
the panel controller off between updates. The HAT PWR line remains enabled so
the controller RAM needed for fast partial refreshes is retained.

### INA228 addressing

The code assumes:

- `0x40` = battery output monitor
- `0x41` = solar input monitor

Those addresses are defined in `include/config.h`.

### Optional DFRobot SEN0657 weather station

The station is disabled in the Opta profile and enabled in the Giga profile.
Only enable it on Opta after configuring and wiring it as follows:

- **Modbus address:** The factory address is `1`, which conflicts with the Solinst
  301's configured address. Change the weather station address register `0x07D0`
  to `2` (the configured `WEATHER_MODBUS_ID`) while the station is isolated from
  the Solinst bus. The standalone
  `weather_station_configurator/weather_station_configurator.ino` sketch performs
  this one-time address and baud-rate configuration, then verifies it.
- **Baud rate:** Change the weather station baud-rate register `0x07D1` to `3`
  for 19200 baud, matching `WEATHER_BAUD`.
- **Serial framing:** DFRobot specifies 8N1 for the weather station; the Solinst
  uses 19200 8E1. The firmware reinitializes the Opta RS-485 interface before
  each device read so both can share the physical bus. Do not change
  `WEATHER_SERIAL_CFG` to the Solinst framing.
- **Bus wiring:** Connect RS-485 A/B consistently to the existing bus and
  provide the station's required power separately. Keep the bus topology,
  termination, and common reference appropriate for the cable run.

The logger samples weather every five minutes in RAM and uploads one summary
with each hourly lake reading. The hourly row contains the latest raw weather
values plus min/max/average temperature, humidity, pressure, wind speed, and
illumination. Wind direction is a circular average, so north readings near
`0` and `360` degrees average correctly.

- wind speed, direction, air temperature, relative humidity, barometric pressure,
  and illumination are instantaneous readings;
- `weather_rainfall_mm` is the station's accumulated rainfall counter in mm, not
  rain during that logger interval. It persists until the station is reset or its
  rainfall-zeroing register is explicitly written.
- `weather_rainfall_interval_mm` is the change in that counter during the
  summary interval. It is `null` and `weather_rainfall_counter_reset` is `true`
  if the counter decreases, which covers a reset, manual zero, rollover, or
  replacement without inventing a rainfall value.

Weather read failure does not discard a successful Solinst reading. The upload
contains the weather error and validity fields instead.

### OLED wake recovery

Each OLED wake restarts the Opta I2C controller before reinitializing the
display. This recovers controller-side I2C state after a display sleep/wake
failure. If the OLED holds SDA or SCL electrically low, firmware cannot clear
that physical condition; inspect the display power, connector, pull-ups, and
I2C cable length.

### INA228 measurement intent

- **battery output monitor** measures the positive feed from the battery/system bus into the logger/load branch
- **solar input monitor** measures the positive feed from the solar charge controller into the battery side

For the solar monitor, the battery side is the **load** side. To read charging current as positive:

- charge controller positive -> `VIN+`
- battery positive / battery bus -> `VIN-`

### OLED display assumptions

The display code currently assumes:

- **SSD1309 controller**
- **128x64** resolution
- **I2C mode**
- default I2C address **`0x3C`**
- U8g2 constructor `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C`

If your display is strapped for a different I2C address, update the value in `include/config.h`.

### Battery charge estimate

`battery_charge_level_pct_approx` is a **rough voltage-based LiFePO4 estimate only**. It is useful for quick visibility, but it is **not** a true coulomb-counted state of charge.

### Solar charging boolean

`solar_charging_battery` is `true` when the solar INA228 is valid and reports current above a small threshold.

---

## Board-specific boundaries

Protocol, scheduling, upload, HTTP, backlog, weather aggregation, power
presentation, and e-paper refresh policy are shared. Concrete RS-485 and
display behavior are selected by the PlatformIO environment.

### Opta-specific items in the current design

1. **Built-in RS-485 hardware**
   - The logger assumes the board already has an RS-485 interface available through the Opta APIs and wiring.
   - `src/probe_modbus.cpp` uses `ArduinoRS485` and timing that were tuned during Opta bring-up.

2. **Built-in Wi-Fi on the Opta WiFi variant**
   - The project assumes the board can use the `WiFi` stack directly for NTP, the local web server, and HTTPS uploads.

3. **Opta status LEDs**
   - `src/ui_status.cpp` assumes the board core exposes Opta LED aliases and uses those for heartbeat, sensor, network, and power state indications.

4. **Opta user button**
   - The OLED wake behavior assumes a board-level user button is available through the Opta core.

5. **Opta RS-485 timing behavior**
   - The current manual Modbus path documents and works around Opta-specific RS-485 behavior, including TX echo showing up in RX.

6. **Physical integration assumptions**
   - The README and current wiring assumptions assume the Opta form factor, integrated I/O, and Opta-side I2C/RS-485 access.

### What is mostly portable?

These parts are conceptually portable to many other Arduino-compatible boards:

- JSON payload structure
- Google Apps Script / Google Sheet logging model
- INA228 reading logic
- SSD1309 OLED content and timeout behavior
- battery estimate / solar charging derived logic
- backlog and upload cooldown strategy
- HTTP endpoint behavior
- secrets/header-based configuration workflow

---

## Porting to a different Arduino board

If you want to support a different board, such as a board that needs an **external RS-485 shield**, **external button**, and **external LEDs**, the main work items are below.

### 1. RS-485 hardware layer

If the target board does not have built-in RS-485, you will need:

- an RS-485 transceiver or shield
- the correct UART selection for that board
- driver-enable / receiver-enable control if the shield requires it

What will likely need to change:

- `src/probe_modbus.cpp`
- possibly `include/config.h` for different timing values
- board-specific RS-485 initialization and direction-control logic

Examples of things you may need to adapt:

- which serial port is used
- whether the RS-485 library supports the board directly
- whether you must manually drive DE/RE pins
- whether the board exhibits the same TX echo behavior as the Opta

### 2. Wi-Fi or network stack

If the target board is not an Opta WiFi, you may need to replace or adapt:

- `WiFi`
- `WiFiUDP`
- the local `WiFiServer`
- HTTPS client behavior used by `ArduinoHttpClient`

Possible cases:

- board with built-in Wi-Fi but a different library/API
- Ethernet-based board instead of Wi-Fi
- cellular transport instead of Wi-Fi

Files most likely affected:

- `src/time_sync.cpp`
- `src/http_api.cpp`
- `src/google_upload.cpp`
- `include/config.h`

### 3. LEDs and button input

If the target board does not expose Opta-style LED and button aliases, you will need to map these functions to:

- discrete LEDs on GPIO pins
- an external momentary pushbutton on a GPIO pin

What will likely need to change:

- `src/ui_status.cpp`
- any board pin definitions in `include/config.h`

Recommended adaptation:

- define explicit pin constants for heartbeat LED, sensor LED, network LED, power LED, and user button
- update the UI code to use those pins instead of Opta-specific aliases

### 4. Display bus/pin mapping

The OLED content logic is portable, but the target board may need different:

- I2C pins
- I2C instance
- voltage compatibility checks

If the board uses software I2C or different hardware I2C wiring, `src/display_oled.cpp` and board setup may need adjustment.

### 5. Power / monitor wiring assumptions

The INA228 logic is portable, but on a different board you still need:

- 3.3 V-compatible I2C
- common ground with the MCU
- the same current-path orientation assumptions

### 6. Build/package assumptions

The supported production environments are `env:opta` and `env:giga`.

---

## Minimum changes for a non-Opta Arduino with RS-485 shield and external button

If you wanted to move this to a different Arduino-compatible board, the minimum likely changes would be:

1. **Replace the Opta RS-485 assumptions**
   - adapt `src/probe_modbus.cpp` behind a target-specific RS-485 adapter
   - add DE/RE pin handling if required

2. **Replace Opta LED/button dependencies**
   - map LEDs to GPIO pins, or disable the LED UI
   - map the display wake button to a normal digital input pin with pull-up/pull-down as needed

3. **Verify network stack compatibility**
   - confirm `WiFi`, `WiFiServer`, `WiFiSSLClient`, and `ArduinoHttpClient` work on that board
   - otherwise adapt networking code

4. **Retune timing**
   - RS-485 delays and response timeout in `include/config.h` may need adjustment on a different MCU / transceiver combination

5. **Update README/build instructions**
   - board package
   - wiring guide
   - pin map

---

## Recommended configuration workflow

The recommended workflow is **compile-time local secrets**.

### Why

This project is maintained in GitHub, but device-specific values such as Wi-Fi credentials, deployment IDs, and shared secrets should not be tracked in the repository.

Compile-time local secrets give you:

- values included in the firmware image at build time
- no dependency on runtime filesystem file I/O for normal deployment
- lower risk of accidentally committing real secrets
- a simple per-developer or per-device local workflow

### Files used

- `secrets_example.h` - tracked template with safe placeholders
- `secrets_local.h` - local untracked file with real values
- `.gitignore` ignores `secrets_local.h`

### Setup steps

1. Copy `secrets_example.h` to `secrets_local.h`
2. Fill in your real values in `secrets_local.h`
3. Build and upload normally
4. Do **not** commit `secrets_local.h`

### Example `secrets_local.h`

```cpp
#pragma once

#define WIFI_SSID_VALUE "your-real-ssid"
#define WIFI_PASS_VALUE "your-real-password"
#define DEVICE_ID_VALUE "opta-well-01"
#define SHARED_SECRET_VALUE "your-long-random-secret"
#define DEPLOYMENT_ID_VALUE "AKfycbxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

### Hard-fail behavior

The build now **fails by default** if `secrets_local.h` is missing.

That is intentional. It prevents accidentally flashing a device with placeholder/example values.

If you intentionally want a placeholder or non-production build, define:

```cpp
ALLOW_PLACEHOLDER_SECRETS
```

and the project will fall back to `secrets_example.h`.

### Secret vs non-secret settings

`secrets_local.h` is now intended only for secrets and deployment-specific identifiers.

Non-secret tuning values stay in tracked config. For example, the OLED display timeout lives in `include/config.h` as `DISPLAY_ON_SECONDS_DEFAULT`.

### Current behavior in code

`include/config.h` includes `secrets_local.h` if it exists. If it does not exist, the build errors unless `ALLOW_PLACEHOLDER_SECRETS` is defined.

`src/config_file.cpp` initializes the derived POST path and prints a summary of the compile-time values being used.

---

## Solinst Modbus behavior

By default, the logger now uses a **fixed configured Solinst Modbus ID** instead of scanning the whole startup range.

In `include/config.h`:

- `WLTS_USE_FIXED_MODBUS_ID = true`
- `WLTS_FIXED_MODBUS_ID = 1`

If you intentionally want startup scanning again, set `WLTS_USE_FIXED_MODBUS_ID = false` and the code will scan `SCAN_START_ID` through `SCAN_END_ID`.

The RS-485 path also uses explicit Opta-specific pre/post delays and an echo-aware manual Modbus parser in `src/probe_modbus.cpp`.

If you port to a different RS-485 transceiver or board, these settings may need retuning.

---

## Opta LEDs, user button, and display

The sketch includes a practical field UI using the Opta status LEDs and the user button.

### LED roles

If the board core exposes the expected Opta LED pin aliases, the sketch uses four LEDs as follows:

- **LED 1** - heartbeat / main loop alive
- **LED 2** - Solinst sensor / Modbus state
- **LED 3** - Wi-Fi / upload state
- **LED 4** - power / charging state

### LED behavior summary

- **Heartbeat LED**
  - brief periodic pulse while the main loop is running

- **Sensor LED**
  - slow blink: no Solinst sensor found yet
  - solid on: sensor found and recent reads are succeeding
  - fast blink: recent probe attempt failed

- **Network LED**
  - slow blink: Wi-Fi disconnected / reconnecting
  - solid on: Wi-Fi connected
  - upload trouble should be interpreted together with `/status`, especially cached upload error state and cooldown

- **Power LED**
  - solid on: solar appears to be charging the battery
  - warning blink: battery estimate is low
  - double-pulse style warning: neither INA228 monitor appears valid

### User button behavior

If the board core exposes the expected user-button alias:

- **button press** - wake the OLED display
- the display stays on for a configurable number of seconds
- when the timeout expires, the display returns to power-save mode

### Display behavior

The OLED is initialized in **power-save mode**. When the user presses the Opta button:

- the display wakes
- the current snapshot is drawn
- the display remains on until the configured timeout expires
- the display is then blanked and returned to power-save mode

Important: this is **software display sleep / power-save**, not true hardware power disconnection. The display appears off to the user, but it is not physically disconnected from power.

If the core does **not** expose the expected LED/button pin aliases, the UI logic compiles in a no-op mode and the logger still runs normally.

On a non-Opta board, you would typically replace these board aliases with normal GPIO pin definitions for LEDs and the external button.

---

## Required Arduino libraries

PlatformIO installs these from `platformio.ini`:

- `ArduinoRS485`
- `ArduinoHttpClient`
- `NTPClient`
- `Adafruit INA228`
- `U8g2`

Built-in/core libraries also used:

- `WiFi`
- `WiFiUdp`
- `Wire`
- `time.h`

---

## Board package

PlatformIO uses the `ststm32` platform, the `opta` board definition, and the
Arduino Mbed framework. Because core releases can change pin aliases, verify the
selected platform version and its Opta aliases if the LED/button UI does not
compile or activate.

---

## How to compile

1. Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).
2. Copy `secrets_example.h` to `secrets_local.h` and fill in real values.
3. Build with `pio run -e opta`.
4. Upload with `pio run -e opta -t upload`.

If compilation fails because `secrets_local.h` is missing, create it from the example template. Only use `ALLOW_PLACEHOLDER_SECRETS` for deliberate placeholder builds.

For a build that deliberately uses tracked placeholders:

```sh
pio run -e opta-placeholder
```

Build or upload the Giga production profile after configuring
`include/giga_board_config.h`:

```sh
pio run -e giga
pio run -e giga -t upload
```

The compile-only Giga placeholder is:

```sh
pio run -e giga-placeholder
```

Run the hardware-independent unit tests on the host:

```sh
pio test -e native -e native-giga
```

See [Testing strategy](docs/testing.md) for the complete automated, HIL,
fault-injection, and soak test plan.

Compile the non-deployable Giga HIL placeholder used by CI:

```sh
pio run -d hil/giga_hardware_hil -e giga-hil-placeholder
```

For a wired bench Giga, configure and flash the real HIL environment as
described in `hil/giga_hardware_hil/README.md`, then run:

```sh
python3 tools/giga_hil.py /dev/cu.usbmodem101 --suite sensors
```

---

## Google Sheets deployment

The repository includes `google_apps_script.gs`.

### What it does

- accepts HTTP POSTs from the Opta
- validates `SHARED_SECRET`
- writes to a yearly tab based on the UTC timestamp
- creates the year tab if it does not exist
- writes the full raw JSON payload into a `raw_json` column

### Deploy steps

1. Create or open the destination Google Sheet.
2. Name it as desired.
3. Open **Extensions -> Apps Script** from that sheet.
4. Paste in the contents of `google_apps_script.gs`.
5. Set the `SHARED_SECRET` in the script to the same value used in `secrets_local.h`.
6. Save the project.
7. Deploy it as a **Web app**.
8. Copy the **deployment ID** from the web app URL.
9. Put that deployment ID into `secrets_local.h` as `DEPLOYMENT_ID_VALUE`.

### Important Apps Script notes

- The script is designed to be a **bound script** attached to the target spreadsheet.
- It uses `@OnlyCurrentDoc` to keep spreadsheet access limited to the bound spreadsheet.
- The Opta posts to the web app URL path constructed from the deployment ID.

---

## Upload endpoint selection

`include/config.h` selects the fesLabs ingest service by default:

```cpp
constexpr UploadEndpointMode UPLOAD_ENDPOINT_MODE =
  UploadEndpointMode::FESLABS_INGEST;
```

To use the legacy Google Apps Script service, set it to:

```cpp
constexpr UploadEndpointMode UPLOAD_ENDPOINT_MODE =
  UploadEndpointMode::GOOGLE_APPS_SCRIPT;
```

The tracked production defaults point to the Firebase Function at
`https://feslabs.com/api/lake/ingest`. These constants control that target in
`include/config.h`:

- `FESLABS_INGEST_HOST` - `feslabs.com`, without `https://`
- `FESLABS_INGEST_PATH` - absolute request path beginning with `/`
- `FESLABS_INGEST_PORT` - normally `443` for HTTPS
- `FESLABS_INGEST_USE_HTTPS` - keep `true` in production; `false` permits a
  plain-HTTP development endpoint

The serial config summary prints the selected mode, host, path, port, and HTTPS
setting at boot. `DEPLOYMENT_ID_VALUE` remains in the secrets template for
Google Apps Script compatibility; it is ignored when `FESLABS_INGEST` is
selected.

### fesLabs ingest service contract

The service should implement `POST FESLABS_INGEST_PATH` and:

1. Accept `Content-Type: application/json` with the same payload documented in
   **Uploaded fields** below. Authentication currently uses the payload's
   `secret` property, matching Apps Script.
2. Validate `secret`, `device_id`, `timestamp_utc`, and the measurement values.
3. Treat `(device_id, timestamp_utc)` as an idempotency key. The firmware has an
   in-memory backlog and retries requests, so repeated delivery must not create
   duplicate readings. Return `2xx` for a replay of an already accepted reading
   (for example, `{"ok":true,"duplicate":true}`).
4. Return any `2xx` status only after the reading has been durably accepted. A
   compact JSON response such as `{"ok":true}` is recommended but the firmware
   does not require a particular response body.
5. Return a non-`2xx` status for rejected or failed writes. Suggested responses
   are `400` for invalid JSON/fields, `401` or `403` for a bad secret, `409` when
   the same idempotency key is reused with conflicting data, `429` for
   throttling, and `5xx` for transient server errors.
6. Avoid redirects. Unlike the Google mode, fesLabs mode accepts only `2xx` as
   success, so redirects and all other status codes enter the existing retry,
   backlog, and cooldown flow.

Serve the production endpoint over HTTPS with a certificate trusted by the
Arduino Opta networking stack. The firmware sends the existing JSON schema
unchanged so the Apps Script and fesLabs destinations can coexist during
migration.

The fesLabs implementation lives in the private `fes/feslabs-web` repository as
a Firebase HTTPS Function. Its `LAKE_LOGGER_SHARED_SECRET` Secret Manager value
must match `SHARED_SECRET_VALUE` in this firmware's untracked
`secrets_local.h`. Firebase Hosting routes `/api/lake/ingest` to that Function.

---

## Uploaded fields

Each successful reading uploads these main values:

### Solinst fields

- `timestamp_utc`
- `device_id`
- `modbus_id`
- `serial_number`
- `firmware`
- `water_level_m`
- `temperature_c`

### Battery output INA228 fields

- `battery_output_monitor_present`
- `battery_output_monitor_valid`
- `battery_output_voltage_v`
- `battery_output_current_a`
- `battery_output_power_w`

### Solar input INA228 fields

- `solar_input_monitor_present`
- `solar_input_monitor_valid`
- `solar_input_voltage_v`
- `solar_input_current_a`
- `solar_input_power_w`

### Derived fields

- `battery_charge_level_pct_approx`
- `solar_charging_battery`

---

## Local HTTP endpoints

The Opta exposes these endpoints over its local web server:

- `/status` - returns cached, pretty-printed device status JSON
- `/probe` - performs an immediate live Solinst probe read and returns pretty-printed JSON
- `/reset` - reboots the Opta
- any unknown route returns a small HTML landing page with clickable links to `/status` and `/probe`, plus a reset button with browser confirmation

### `/status` includes

- Wi-Fi status and IP
- uptime
- clock validity and sync age
- selected upload endpoint mode, host, path, port, and HTTPS setting
- Solinst sensor identity
- last successful probe/upload timestamps
- cached upload error string and timestamp
- upload cooldown remaining
- consecutive upload failures
- successful/transient-failure upload counts
- permanent upload rejection and backlog-drop counts
- last permanent rejection HTTP status, error, rejection timestamp, and
  rejected reading timestamp
- backlog counts
- INA228 presence/validity
- cached probe battery/solar values kept with the last lake reading
- latest manager battery/solar voltage/current/power values, refreshed every
  10 seconds independently of HTTP requests and clock validity
- approximate battery charge percent
- boolean solar charging status

### Notes on `/status`

`/status` is intentionally cached/non-blocking. It does **not** trigger a
power-monitor refresh or live Modbus read. The compatibility fields prefixed
with `live_` contain the latest periodically refreshed manager snapshot, not
request-time I2C reads. `cached_probe_*` remains the power snapshot attached to
the last successful lake reading. A successful live or scheduled probe
force-refreshes manager power before copying it into that reading.

### Notes on `/probe`

`/probe` is intentionally a live operation, so it can still take noticeably longer than `/status` if Modbus retries are happening.

---

## Upload/backlog behavior

Uploads use cached failure state plus cooldown/backoff so a broken or
misconfigured remote endpoint does not get hammered continuously.

The backlog is intentionally RAM-backed and does not survive reset or power
loss. Persistent storage is a gated design only; see
[Persistent backlog: staged design and safety gate](docs/persistent-backlog.md).
Normal firmware does not initialize, format, erase, or write QSPI/internal flash
for backlog storage.

### Current behavior

- transient results use up to `POST_RETRIES`; all `2xx` responses are accepted
- transport errors, HTTP 408/425/429, unknown redirects, and `5xx` responses
  are transient
- Google Apps Script 301/302/303/307/308 responses remain accepted; fesLabs
  redirects are never accepted
- request/auth/conflict/size/type failures
  (400/401/403/404/409/413/415/422) and other `4xx` responses are permanent
  rejections
- after an exhausted transient operation, the device records the last upload
  error and timestamp
- a cooldown is applied once per completed logical upload operation, not once
  per wire attempt
- the cooldown starts at `UPLOAD_RETRY_COOLDOWN_INITIAL_MS`
- it backs off up to `UPLOAD_RETRY_COOLDOWN_MAX_MS`
- backlog flush attempts also respect the cooldown
- cooldown and Wi-Fi deferrals retain or enqueue readings without incrementing
  failure or rejection counters
- permanently rejected fresh readings are counted and not queued
- permanently rejected backlog heads are counted, dropped, and dequeued so
  later FIFO readings can progress

This makes the logger much less likely to starve the HTTP server or local UI when the remote endpoint is broken.

---

## Logging behavior

- The logger runs on **hourly UTC boundaries, on the hour**.
- The system waits until the clock is valid before scheduled logging begins.
- If an upload fails, the reading is queued in a small in-memory backlog and retried later.
- Upload retries are now throttled by cooldown/backoff after repeated failures.
- The OLED display remains in power-save mode until the user presses the Opta button.
- Once woken, the display remains on for `DISPLAY_ON_SECONDS_DEFAULT` and then returns to power-save mode.

---

## Current limitations / notes

- The Solinst 301 is expected to already be configured for **Modbus RTU** with matching serial settings.
- The battery charge percentage is an **approximation**, not a true state-of-charge algorithm.
- The code assumes the **Adafruit INA228** Arduino library API.
- The code assumes you have physical access to the Opta I2C bus for the INA228 boards and OLED.
- The INA228 current/power calibration depends on the actual shunt value on your monitor boards.
- The Opta LED/button UI depends on the board core exposing compatible LED and button pin aliases.
- The OLED "off" behavior is software power-save, not physical power removal.
- Cached upload errors are exposed via `/status`, but they are not yet rendered on the OLED.
- `/status` exposes permanent rejection/drop counters and the last permanent
  status, error, rejection timestamp, and rejected reading timestamp.

---

## Typical bring-up checklist

1. Confirm the Opta sketch compiles.
2. Create `secrets_local.h` from `secrets_example.h`.
3. Fill in real local values in `secrets_local.h`.
4. Confirm serial output appears at boot.
5. Confirm the config summary reports compile-time secrets.
6. Confirm the expected fixed Solinst Modbus ID is correct, or intentionally re-enable scanning.
7. Confirm both INA228 devices are detected at `0x40` and `0x41`.
8. Confirm the SSD1309 OLED is detected and can wake on button press.
9. Confirm the Solinst 301 responds on Modbus.
10. Confirm Wi-Fi connects.
11. Confirm the Apps Script web app responds.
12. Confirm data appears in the correct yearly tab in Google Sheets.
13. Confirm LED behavior matches the expected field UI.
14. Confirm the display turns off again after the configured timeout.
15. Confirm `/status` shows sane upload error/cooldown state if the remote endpoint is intentionally broken during testing.

---

## Security notes

- Do **not** commit real secrets to source control.
- Keep real values in `secrets_local.h` on the build machine.
- `secrets_local.h` is intentionally git-ignored.
- Keep the Apps Script `SHARED_SECRET` synchronized with the value in `secrets_local.h`.

---

## Future improvements

Possible next steps:

- true coulomb-counted battery SOC
- richer `/probe` output including INA228 fields
- true switched-power control for the display instead of software power-save
- show cached upload/system errors on the OLED
- implement the gated persistent backlog design after its bench prerequisites
- OTA update path
- more robust error classification for Solinst and upload failures
