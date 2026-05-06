# Solinst Lake Logger

A field logger for an **Arduino Opta WiFi** that:

- reads **water level** and **temperature** from a **Solinst 301** over **Modbus RTU / RS-485**
- timestamps readings with NTP-synchronized UTC time
- exposes local HTTP endpoints for health and diagnostics
- uploads readings to a **Google Apps Script** web app that writes into a Google Sheet
- reads runtime configuration from `/user/config.ini` on the Opta QSPI user-data filesystem
- reads two **INA228** I2C power monitors:
  - **battery output** monitor at **0x40**
  - **solar input** monitor at **0x41**

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
- `config.h` - compile-time defaults, globals, prototypes, monitor addresses
- `config_file.ino` - runtime config loader from `/user/config.ini`
- `util.ino` - utility helpers
- `backlog.ino` - upload backlog queue
- `time_sync.ino` - Wi-Fi and NTP time sync
- `probe_modbus.ino` - Solinst Modbus reads
- `power_monitors.ino` - INA228 initialization and reads
- `google_upload.ino` - JSON payload generation and upload
- `http_api.ino` - local HTTP handlers
- `google_apps_script.gs` - Google Apps Script endpoint for Sheets logging
- `config.ini.example` - example runtime config file

---

## Hardware assumptions

This project is written for:

- **Arduino Opta WiFi**
- **Solinst 301** wired for **Modbus RTU** over the Opta RS-485 interface
- **2x INA228** boards on I2C
  - battery output monitor at `0x40`
  - solar input monitor at `0x41`
- **12 V LiFePO4** battery system with solar charge controller

### INA228 addressing

The code assumes:

- `0x40` = battery output monitor
- `0x41` = solar input monitor

Those addresses are defined in `config.h`.

### INA228 measurement intent

- **battery output monitor** measures the positive feed from the battery/system bus into the logger/load branch
- **solar input monitor** measures the positive feed from the solar charge controller into the battery side

### Battery charge estimate

`battery_charge_level_pct_approx` is a **rough voltage-based LiFePO4 estimate only**. It is useful for quick visibility, but it is **not** a true coulomb-counted state of charge.

### Solar charging boolean

`solar_charging_battery` is `true` when the solar INA228 is valid and reports current above a small threshold.

---

## Required Arduino libraries

Install these in **Arduino IDE** using Library Manager:

- `ArduinoRS485`
- `ArduinoModbus`
- `ArduinoHttpClient`
- `NTPClient`
- `Adafruit INA228`

Built-in/core libraries also used:

- `WiFi`
- `WiFiUdp`
- `Wire`
- `time.h`

---

## Board package

Install the correct **Arduino Opta / Mbed OS** board package in Arduino IDE and select the **Arduino Opta WiFi** board before compiling.

---

## How to compile

1. Open the repository folder in **Arduino IDE**.
2. Make sure the sketch tabs/files are all present.
3. Install the required libraries.
4. Select the **Arduino Opta WiFi** board.
5. Select the correct USB port.
6. Click **Verify**.
7. Click **Upload**.

If compilation fails on missing libraries, install them first and restart the IDE if needed.

---

## Runtime configuration

This project does **not** require secrets to be compiled into the sketch.

At boot it tries to load runtime values from:

`/user/config.ini`

on the Opta QSPI user-data filesystem.

### Supported keys

The loader reads these keys:

- `WIFI_SSID`
- `WIFI_PASS`
- `DEVICE_ID`
- `SHARED_SECRET`
- `DEPLOYMENT_ID`

For backward compatibility it also accepts:

- `POST_DEPLOYMENT_ID`
- `POST_PATH`

but those values are interpreted as the **Google Apps Script deployment ID**, not the full path.

### Example config file

Use `config.ini.example` as your template.

Example:

```ini
WIFI_SSID=your-ssid
WIFI_PASS=your-password
DEVICE_ID=opta-well-01
SHARED_SECRET=your-long-random-secret
DEPLOYMENT_ID=AKfycbxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

The code builds the Apps Script path automatically as:

```text
/macros/s/<DEPLOYMENT_ID>/exec
```

---

## Preparing `/user/config.ini` on the Opta

You need a writable **user-data filesystem** on the Opta QSPI flash.

The code will attempt to mount:

- partition 4 first
- then partition 3 as a fallback

and it will try both:

- `LittleFS`
- `FatFS`

Once the user partition exists and is formatted, place your runtime config file at:

```text
/user/config.ini
```

On boot, the serial console prints a runtime config summary including:

- whether the user filesystem mounted
- filesystem type
- partition used
- whether config values came from `config.ini` or defaults
- a masked self-test of the loaded secret values

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
5. Set the `SHARED_SECRET` in the script to the same value used in `/user/config.ini`.
6. Save the project.
7. Deploy it as a **Web app**.
8. Copy the **deployment ID** from the web app URL.
9. Put that deployment ID into `/user/config.ini` as `DEPLOYMENT_ID=...`.

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

---

## Current limitations / notes

- The Solinst 301 is expected to already be configured for **Modbus RTU** with matching serial settings.
- The battery charge percentage is an **approximation**, not a true state-of-charge algorithm.
- The code assumes the **Adafruit INA228** Arduino library API.
- The code assumes you have physical access to the Opta I2C bus for the INA228 boards.

---

## Typical bring-up checklist

1. Confirm the Opta sketch compiles.
2. Confirm serial output appears at boot.
3. Confirm runtime config is loaded from `/user/config.ini`.
4. Confirm both INA228 devices are detected at `0x40` and `0x41`.
5. Confirm the Solinst 301 responds on Modbus.
6. Confirm Wi-Fi connects.
7. Confirm the Apps Script web app responds.
8. Confirm data appears in the correct yearly tab in Google Sheets.

---

## Security notes

- Do **not** commit real secrets to source control.
- Keep real values in `/user/config.ini` on the device.
- Keep the Apps Script `SHARED_SECRET` synchronized with the device config.

---

## Future improvements

Possible next steps:

- true coulomb-counted battery SOC
- richer `/probe` output including INA228 fields
- better persistent backlog storage
- OTA update path
- more robust error classification for Solinst and upload failures
