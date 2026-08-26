# Giga Site Logger Architecture Notes

These notes capture the intended direction for the Arduino Giga R1 site logger path. They are not requirements for the existing Opta deployment.

## Scope split

The current Opta setup should remain conservative and Solinst-focused:

- Solinst 301 water level/temperature
- INA228 power monitoring
- existing local HTTP status/probe/reset API
- existing OLED path if installed
- simple RAM backlog

The Giga setup is the path for broader site capabilities:

- Solinst 301 on its own RS-485 channel
- DFRobot weather station on a separate RS-485 channel
- INA228 power monitoring
- Waveshare 4.26 inch e-paper HAT
- larger local backlog
- future direct fesLabs ingest
- possible future persistent storage

## Display direction

The e-paper display should use a single normal-operation dashboard page. We are not planning to allocate enclosure space for a physical page-rotation button in the first Giga enclosure.

### Refresh behavior

- Normal page should refresh roughly every 15 minutes, aligned with logging where practical.
- The page is static between refreshes.
- If Wi-Fi is disconnected, upload health is poor, or another critical condition is active, the display can automatically switch to a diagnostics-oriented page.
- Richer diagnostics should remain available through the local HTTP API.

### Status bar

Use a thin, phone-style status bar at the top of the display.

Suggested fields:

- Wi-Fi state and signal quality/RSSI indicator
- battery charge percentage
- solar/PV charging indicator
- assigned IP address
- optional warning marker when uploads/backlog/sensors are unhealthy

### Widget layout

Use rounded boxes for the main sections below the status bar.

Water widget:

- water level
- water temperature
- last successful sample time
- Solinst sensor status
- optional trend/delta later

Weather widget:

- air temperature
- humidity
- barometric pressure
- wind speed/direction
- rainfall interval or recent rainfall summary
- weather station status

Power widget:

- battery voltage
- approximate battery percentage
- solar charging state
- solar input voltage/current/power if available
- logger load power if available
- local 24-hour battery minimum and maximum

Upload/status widget:

- last successful upload time
- backlog count
- upload target/mode
- concise last upload error if unhealthy

### 24-hour battery min/max

The Giga firmware should compute local rolling 24-hour battery minimum and maximum. This is useful for winter solar evaluation and should continue to work even when uploads fail.

Implementation should be independent of the display. The display consumes a state snapshot containing the already-computed values.

## Local backlog API

Add local Arduino-served endpoints for recovery when cloud upload is failing.

Proposed endpoints:

```text
GET /backlog/status
GET /backlog.csv
GET /backlog.json
GET /backlog/replay?confirm=1
GET /backlog/clear?confirm=DELETE
```

POST variants are preferred later where feasible, but explicit-confirmation GET endpoints are acceptable for the simple embedded web UI.

### CSV export

`/backlog.csv` should return all queued readings in FIFO order with a header row. The CSV should match the upload/spreadsheet schema as closely as practical so the data can be pasted or imported manually.

### JSON export

`/backlog.json` should expose the same queued readings for debugging or future tools.

### Safe clear

Backlog clear must require explicit confirmation and must report how many readings were cleared. A bare GET must not clear data.

### Replay

Replay should force upload retry without waiting for the normal schedule or cooldown. It should only dequeue entries after confirmed upload success.

## Reading storage abstraction

Avoid baking the current Opta fixed RAM backlog directly into upload, display, or HTTP code.

Introduce a storage/backlog interface with operations like:

- enqueue reading
- peek oldest reading
- dequeue oldest reading
- count/capacity
- iterate readings in FIFO order
- clear with explicit caller intent
- oldest/newest timestamp metadata

Initial backends:

- Opta: small RAM queue, preserve current behavior
- Giga: larger RAM queue, designed for site readings

Future backends:

- SD card
- flash/QSPI if reliable on the selected board
- append-only local log

## Site reading model

The stored/uploaded unit should become a site reading rather than a Solinst-only reading.

Suggested contents:

- timestamp
- device/site identity
- lake/Solinst reading
- weather reading, optional
- power/solar readings
- upload/status metadata
- diagnostics/error fields

This allows one log row to contain partial data. For example, the lake reading can still be valid when the weather station is absent.

## Multi-core readiness

The Giga implementation should start single-core unless there is a measured reason to split work across cores. The normal Arduino loop model is simpler to bring up and debug.

However, code boundaries should leave a future dual-core implementation possible.

Manager boundaries that should stay clean:

- sensor manager
- upload manager
- display renderer
- local HTTP/API server
- storage/backlog manager
- power monitor manager
- clock/time sync manager

Prefer snapshots and explicit interfaces over shared mutable globals.

Examples:

- display consumes a `SiteState`/`SiteSnapshot`, not live sensor globals
- upload consumes queued `SiteReading` records from storage
- HTTP endpoints use status/storage interfaces, not raw queue globals
- sensor drivers publish readings/events rather than updating display/upload state directly

Possible future split, non-binding:

- M7: Wi-Fi, HTTP API, uploads, e-paper rendering, high-level scheduler
- M4: RS-485 polling, INA228 reads, low-level sensor timing

If that split is ever implemented, use RPC/message passing or a very narrow shared data interface.

## Related issues

- Direct fesLabs ingest endpoint: #2
- Giga e-paper dashboard: #3
- Local backlog export/clear/replay endpoints: #4
- Reading storage abstraction and larger Giga backlog: #5
- Multi-core readiness: #6
