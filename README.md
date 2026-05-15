# Solinst Lake Logger

A field logger for an **Arduino Opta WiFi** that:

- reads **water level** and **temperature** from a **Solinst 301** over **Modbus RTU / RS-485**
- timestamps readings with NTP-synchronized UTC time
- exposes local HTTP endpoints for health and diagnostics
- uploads readings to a **Google Apps Script** web app that writes into a Google Sheet
- reads two **INA228** I2C power monitors:
  - **battery output** monitor at **0x40**
  - **solar input** monitor at **0x41**
- uses the Opta status LEDs for field diagnostics
- supports a **2.42 inch SSD1309 I2C OLED** local status display that wakes on user-button press and turns back off after a configurable timeout

The logger currently uploads:

- timestamp
- Solinst water level and temperature
- Solinst identity metadata (Modbus ID, serial number, firmware)
- INA228 battery output voltage/current/power
- INA228 solar input voltage/current/power
- an approximate battery charge percentage
- a boolean indicating whether solar appears to be charging the battery

---

## Repository layout

- `Solinst_Lake_Logger.ino` - main setup/loop
- `config.h` - compile-time defaults, globals, prototypes, monitor addresses, and tracked non-secret defaults
- `config_file.ino` - compile-time config initialization and summary output
- `secrets_example.h` - tracked template for local secrets
- `util.ino` - utility helpers
- `backlog.ino` - upload backlog queue
- `time_sync.ino` - Wi-Fi and NTP time sync
- `probe_modbus.ino` - Solinst Modbus reads
- `probe_cycle.ino` - shared probe + upload flow
- `power_monitors.ino` - INA228 initialization and reads
- `battery_status.ino` - shared battery estimate / charging status helpers
- `ui_status.ino` - Opta LED behavior and user-button handling
- `display_oled.ino` - SSD1309 OLED init / wake / timeout / redraw logic
- `google_upload.ino` - JSON payload generation and upload
- `http_api.ino` - local HTTP handlers
- `google_apps_script.gs` - Google Apps Script endpoint for Sheets logging

---

## Hardware assumptions

This project is written for:

- **Arduino Opta WiFi**
- **Solinst 301** wired for **Modbus RTU** over the Opta RS-485 interface
- **2x INA228** boards on I2C
  - battery output monitor at `0x40`
  - solar input monitor at `0x41`
- **2.42 inch SSD1309 128x64 I2C OLED** display
- **12 V LiFePO4** battery system with solar charge controller

### INA228 addressing

The code assumes:

- `0x40` = battery output monitor
- `0x41` = solar input monitor

Those addresses are defined in `config.h`.

### INA228 measurement intent

- **battery output monitor** measures the positive feed from the battery/system bus into the logger/load branch
- **solar input monitor** measures the positive feed from the solar charge controller into the battery side

### OLED display assumptions

The display code currently assumes:

- **SSD1309 controller**
- **128x64** resolution
- **I2C mode**
- default I2C address **`0x3C`**
- U8g2 constructor `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C`

If your display is strapped for a different I2C address, update the value in `config.h`.

### Battery charge estimate

`battery_charge_level_pct_approx` is a **rough voltage-based LiFePO4 estimate only**. It is useful for quick visibility, but it is **not** a true coulomb-counted state of charge.

### Solar charging boolean

`solar_charging_battery` is `true` when the solar INA228 is valid and reports current above a small threshold.

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

Non-secret tuning values stay in tracked config. For example, the OLED display timeout now lives in `config.h` as `DISPLAY_ON_SECONDS_DEFAULT`.

### Current behavior in code

`config.h` includes `secrets_local.h` if it exists. If it does not exist, the build errors unless `ALLOW_PLACEHOLDER_SECRETS` is defined.

`config_file.ino` initializes the derived POST path and prints a summary of the compile-time values being used.

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
  - solid on: Wi-Fi connected and no obvious backlog condition
  - activity/fault patterns may be extended further as upload attempt tracking evolves

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

---

## Required Arduino libraries

Install these in **Arduino IDE** using Library Manager:

- `ArduinoRS485`
- `ArduinoModbus`
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

Install the correct **Arduino Opta / Mbed OS** board package in Arduino IDE and select the **Arduino Opta WiFi** board before compiling.

Because Opta core releases can change pin alias naming, if the LED/button UI does not compile or does not activate, verify the installed core version and the available LED/button pin aliases for your Opta board package.

---

## How to compile

1. Open the repository folder in **Arduino IDE**.
2. Make sure the sketch tabs/files are all present.
3. Install the required libraries.
4. Create `secrets_local.h` from `secrets_example.h` and fill in real values.
5. Select the **Arduino Opta WiFi** board.
6. Select the correct USB port.
7. Click **Verify**.
8. Click **Upload**.

If compilation fails because `secrets_local.h` is missing, create it from the example template. Only use `ALLOW_PLACEHOLDER_SECRETS` for deliberate placeholder builds.

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

- `/probe` - performs an immediate Solinst probe read and returns JSON
- `/status` - returns diagnostics and current status
- `/reset` - reboots the Opta

### `/status` includes

- Wi-Fi status and IP
- uptime
- clock validity and sync age
- Solinst sensor identity
- last successful probe/upload timestamps
- backlog counts
- INA228 presence/validity
- live battery and solar voltage/current/power values
- approximate battery charge percent
- boolean solar charging status

---

## Logging behavior

- The logger runs on **even 15-minute UTC boundaries**.
- The system waits until the clock is valid before scheduled logging begins.
- If an upload fails, the reading is queued in a small in-memory backlog and retried later.
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

---

## Typical bring-up checklist

1. Confirm the Opta sketch compiles.
2. Create `secrets_local.h` from `secrets_example.h`.
3. Fill in real local values in `secrets_local.h`.
4. Confirm serial output appears at boot.
5. Confirm the config summary reports compile-time secrets.
6. Confirm both INA228 devices are detected at `0x40` and `0x41`.
7. Confirm the SSD1309 OLED is detected and can wake on button press.
8. Confirm the Solinst 301 responds on Modbus.
9. Confirm Wi-Fi connects.
10. Confirm the Apps Script web app responds.
11. Confirm data appears in the correct yearly tab in Google Sheets.
12. Confirm LED behavior matches the expected field UI.
13. Confirm the display turns off again after the configured timeout.

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
- better persistent backlog storage
- OTA update path
- more robust error classification for Solinst and upload failures
