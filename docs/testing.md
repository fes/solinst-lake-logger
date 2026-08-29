# Testing strategy

This project uses a risk-based test pyramid. Fast automated tests cover portable
logic and interface validation on every change. Compile checks catch integration
and dependency errors. Hardware-in-the-loop (HIL) tests then exercise the real
Opta, sensor buses, network, clock, and peripherals. Long-duration and physical
fault tests remain manual because CI cannot reproduce field wiring, radio
conditions, power loss, or timer duration.

## Safety and prerequisites

- Run commands from the `solinst-lake-logger` repository root unless noted.
- Install Python 3 and [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html).
- For web ingest tests, install the Node version required by
  `../feslabs-web/functions/package.json` and run `npm ci` there first.
- For HIL, use separately powered bench hardware with correct voltage levels,
  RS-485 polarity/termination, and an operator-reviewed pin map. Never run bus
  tests on a production installation.
- Use a test device ID and test ingest destination for fault and soak work.
  Never inject failures against production data or a field installation.
- `tools/hil_smoke.py` is intentionally read-only with respect to logger
  management: it sends only `GET /status` and `GET /probe`. It never calls
  `/reset` or a backlog-management endpoint. `/probe` does perform one live
  sensor read and force-refreshes power. `/status` only reports the latest
  manager power snapshot; repeated status requests must not create I2C traffic.
- Do not run production `pio run -e opta` in CI: it needs untracked secrets.
  The placeholder environment is compile-only and must never be uploaded.
- Persistent backlog work is gated as described in
  [persistent-backlog.md](persistent-backlog.md). There is currently no
  `opta-persistent` environment, and production remains RAM-backed.

## Test pyramid and commands

### 1. Automated native unit tests

Risk covered: ring-buffer behavior, Modbus CRC/frame scanning, upload status
classification and fresh/backlog actions, retry backoff, rollover-safe
deadlines, periodic power-poll due timing across `millis()` rollover, power
snapshot copy semantics, weather aggregation, logging interval identity and
delayed catch-up, battery estimation, board profiles, and the bounded HTTP
parser/router.

```sh
pio test -e native -e native-giga
```

Expected: the `native` environment reports all Unity tests passed with no
failed or skipped tests. This does not validate Arduino drivers or real wiring.

The scheduler identifies UTC intervals from epoch time. Its first valid clock
observation fires only inside the configured boundary window; a first
observation later in an interval only initializes the state. After that, the
first observation of a newer interval fires once even when blocking work made
the loop late. Forward jumps produce one current reading rather than synthetic
readings for every missed interval. Backward corrections remain suppressed
until time reaches an interval newer than the highest one already observed.
Probe timestamps remain the actual probe time.

HTTP tests cover exact routing for `/`, `/status`, `/probe`, and `/reset`;
prefix and query-string non-matches; unsupported and malformed methods;
malformed spacing and HTTP versions; strict CRLF handling; and the configured
request-line, request-target, header-line, total-header-byte, and header-count
limits. Firmware returns `400` for malformed requests, `404` for unknown
targets, `405` plus `Allow: GET` for unsupported methods on known routes, `414`
for request-line/target overflow, and `431` for header overflow. Current routes
do not accept query strings.

### 2. Automated Python HIL-tool tests

Risk covered: `/status` and `/probe` contract validation, endpoint safety,
malformed responses, content types, HTTP failures, and resource cleanup.

```sh
python3 -W error::ResourceWarning -m unittest discover -s tools/tests -v
```

Expected: all tests pass and no warning is promoted to an error.

### 3. Automated production placeholder compile matrix

Risk covered: Opta and Giga framework/library integration, board-profile
selection, full production application linkage, independent Giga UART channels,
e-paper driver integration, Wi-Fi/NTP/HTTP/upload integration, power monitoring,
weather polling, and profile-sized RAM backlog.

```sh
pio run -e opta-placeholder
pio run -e giga-placeholder
```

Expected: both environments succeed. Do not upload either artifact because
they contain deliberate placeholder configuration.

### 4. Automated hardware HIL firmware compiles

Risk covered: Giga M7 framework compatibility, SC16IS752-over-SPI compilation,
the shared Modbus codec, bounded command parsing, direction-control GPIO, and
e-paper control-input scaffolding.

```sh
pio run -d hil/giga_hardware_hil -e giga-hil-placeholder
```

Expected: the environment compiles with both RS-485 channels and e-paper
disabled. This proves only the firmware scaffold and must not be treated as a
validated pin map or uploaded for hardware acceptance.

The standalone Opta enumerator is read-only at the attached-device level. It
detects the Wi-Fi module, scans I2C, verifies INA228 identity registers, and
reads Solinst input registers without requiring production secrets:

```sh
pio run -d hil/opta_hardware_hil -e opta-hil
```

### 5. Automated feslabs-web ingest contract tests

These tests live in the adjacent repository; this firmware repository does not
modify or run them in its CI.

```sh
cd ../feslabs-web/functions
npm ci
npm test
```

Expected: TypeScript builds and all `test/lake-ingest.test.cjs` Node tests pass,
including authentication, payload validation, deduplication/conflict handling,
header/row mapping, and Sheets failure behavior. No live Sheets writes occur.

### 6. Real Opta HIL smoke

Prerequisites: flash a secrets-configured production build to a bench Opta,
connect the expected probe, obtain the Opta base URL, and ensure the operator can
identify the expected device ID.

```sh
python3 tools/hil_smoke.py http://192.168.1.50 \
  --expected-device-id opta-test-01 \
  --expected-board-profile opta-solinst --timeout 10
```

Expected: the command reports that `/status` and `/probe` are valid. Confirm the
device ID, finite water level/temperature, and plausible peripheral state. A
failed probe or HTTP/JSON/contract error produces a nonzero exit status.

The same production contract runner validates a secrets-configured Giga after
the isolated channel tests pass:

```sh
python3 tools/hil_smoke.py http://192.168.1.51 \
  --expected-device-id giga-test-01 \
  --expected-board-profile giga-site --timeout 10
```

### 7. Giga dual-RS-485 and e-paper HIL

The Giga HIL image is deliberately separate from production firmware and uses
USB serial as its control plane. It has no credentials, uploads, storage
formatting, or Modbus write command. Copy
`hil/giga_hardware_hil/include/hil_config.example.h` to the ignored
`hil_config.h` and set values only from the reviewed wiring diagram.

```sh
pio run -d hil/giga_hardware_hil -e giga-hil
pio run -d hil/giga_hardware_hil -e giga-hil -t upload
python3 -m pip install pyserial
```

Start with the passive suite, which transmits nothing on either hardware bus:

```sh
python3 tools/giga_hil.py /dev/cu.usbmodem101 --suite smoke \
  --json-output giga-smoke.json
```

Set both RS-485 HAT channels to Half-auto/manual mode: positions 3 and 4 ON,
positions 1 and 2 OFF on both SW1 and SW2. After checking A/B polarity,
termination, transceiver power, shared-SPI wiring, and EN1/EN2 polarity,
automate read-only transactions against both devices:

```sh
python3 tools/giga_hil.py /dev/cu.usbmodem101 --suite sensors \
  --solinst-channel 1 --solinst-baud 19200 --solinst-slave 1 \
  --weather-channel 2 --weather-baud 19200 --weather-slave 2 \
  --json-output giga-sensors.json
```

The Solinst check reads two input registers at address `0` using `8E1`. The
weather check reads wind speed at `0x01F4` using `8N1`. A response is accepted
only when slave, function, byte count, length, and CRC are valid. Raw received
bytes and decoded registers are included in the report.

The full standalone HIL suite verifies that the configured e-paper BUSY input
reaches idle. Pixel rendering is exercised by the production Giga firmware,
which uses GxEPD2's exact GDEQ0426T82/SSD1677 driver:

```sh
python3 tools/giga_hil.py /dev/cu.usbmodem101 --suite full \
  --epaper-timeout-ms 30000 --json-output giga-full.json
```

An e-paper reset pulse is an opt-in output test and requires both safety flags:

```sh
python3 tools/giga_hil.py /dev/cu.usbmodem101 --suite full \
  --reset-epaper --allow-output-tests
```

The firmware itself also requires the literal `CONFIRM` protocol token. The
tracked wiring for Waveshare SKU 26376 is D10 CS, D9 DC, D8 RST, D7 BUSY,
D11 MOSI, and D13 SCK. Confirm those connections physically before running an
output test. Automated tests can validate driver compilation, policy, BUSY
timeouts, and refresh counters; final pixel appearance still requires visual
inspection or a camera-based fixture.

### 8. Fault injection

Use a bench device and test ingest target. In one terminal, capture diagnostics
without invoking reset:

```sh
while true; do
  date -u
  curl --fail --silent --show-error http://192.168.1.50/status
  sleep 30
done | tee fault-injection.log
```

Then inject **one controlled fault at a time**: disconnect/reconnect the test
LAN, configure the test ingest service to reject requests, add/remove an
RS-485 echo/noise source, disconnect/reconnect each optional peripheral, or
block/restore NTP. Stop the loop with Ctrl-C. After restoring the fault, run:

```sh
python3 tools/hil_smoke.py http://192.168.1.50 \
  --expected-device-id opta-test-01 --timeout 10
```

Expected: the logger remains responsive where networking permits and records an
actionable status/error. Transient failures retain queued readings and apply one
cooldown per exhausted logical operation. Wi-Fi/cooldown deferrals do not change
failure counters. Permanent `4xx` test responses increment
`permanent_upload_rejections`; a rejected backlog head also increments
`permanent_backlog_drops`, is removed, and allows later FIFO entries to progress.
Never short power rails, hot-plug unsafe current paths, transmit on a production
Modbus bus, or deliberately corrupt production ingest data.

### 9. Soak

Run only on bench hardware with enough time to cross several logging intervals.
The example below performs one read-only status check every five minutes for
24 hours and stores the evidence locally:

```sh
python3 - <<'PY'
import datetime
import json
import time
import urllib.request

url = "http://192.168.1.50/status"
with open("hil-soak.jsonl", "a", encoding="utf-8") as output:
    for _ in range(24 * 12):
        with urllib.request.urlopen(url, timeout=10) as response:
            status = json.load(response)
        output.write(json.dumps({
            "observed_utc": datetime.datetime.now(
                datetime.timezone.utc).isoformat(),
            "status": status,
        }, separators=(",", ":")) + "\n")
        output.flush()
        time.sleep(300)
PY
```

Expected: no lockups or unbounded error growth; uptime advances unless a reboot
is intentionally tested; logging follows configured cadence; backlog stabilizes
or drains after recovery; and probe/upload counters and timestamps remain
coherent. Review `hil-soak.jsonl` before deleting it. A true `millis()` rollover
soak takes about 49.7 days and must include observations before and after wrap.

## Manual and HIL coverage matrix

| Scenario | Automated coverage | HIL procedure | Manual-only acceptance |
| --- | --- | --- | --- |
| Network loss and recovery | Backoff and rollover-safe deadline unit tests | Disconnect/reconnect test LAN while capturing `/status` | Wi-Fi recovers; queued data is not lost or duplicated |
| Ingest rejection | feslabs-web validation/failure tests; firmware status/action/backoff tests | Make the **test** endpoint return transient and permanent responses, then restore it | Transient failures retry/cool down once per operation; permanent failures are diagnosed without poisoning FIFO |
| Backlog retention and replay | Ring-buffer wrap/full/reuse and upload-action unit tests | Sustain transient upload failure across log events, then test a permanent rejection at the head | Transient heads remain; accepted heads dequeue; permanently rejected heads increment counted drops and allow later FIFO entries to progress |
| Modbus echo/noise | CRC, malformed-frame, and offset-scanner unit tests | Introduce controlled echo/noise on the isolated bench bus | Valid replies are found; corrupt replies never become readings; recovery follows noise removal |
| Weather absent | Weather aggregation is automated; presence is not | Use weather-disabled Opta profile or disconnect a bench weather sensor | Lake readings continue and weather fields/status accurately show disabled/absent/invalid |
| INA monitors absent | Battery/charging math, poll timing, and snapshot copying are automated; I2C discovery is not | Boot with each INA monitor disconnected, verify repeated `/status` calls do not trigger reads, then reconnect and reboot | Logger/probe remains usable; manager and cached-probe flags/null measurements are distinct and correct; valid-read timestamps advance only after valid samples |
| OLED absent | Board profile is automated; display driver is not | Boot without OLED, then reconnect/reboot as the test plan permits | No boot blockage; status reports absence; logger continues logging/uploading |
| NTP failure and recovery | Invalid-clock suppression, first-clock initialization, forward jumps, and backward corrections are automated | Block NTP before boot, then restore it | No falsely timestamped or spurious mid-interval logs; clock becomes valid and cadence resumes after sync |
| Reboot | No full-system automated coverage | Record `/status`, power-cycle bench Opta, then rerun HIL smoke | Clean restart, expected uptime reset, sensor rediscovery, and no unexplained backlog loss |
| Logging cadence | UTC interval identity, exact-boundary, delayed crossing, jump, rollover, and deduplication unit tests | Soak across at least three configured intervals and briefly block work across one boundary | At most one real-time reading per observed interval; a delayed loop catches up once using the actual probe time, without replaying missed intervals or duplicating after clock correction |
| `millis()` rollover | Deadline comparison around wrap is automated | 49.7-day soak or instrumented bench firmware only | Retry, display, polling, and scheduling continue across wrap without lockup |

“Automated” means deterministic CI/host coverage. “HIL” requires real bench
hardware and an operator-controlled environment. Items in the final column
require human inspection or long-duration evidence and are not CI claims.

## Persistent backlog acceptance tests (future gated backend)

These tests are requirements for a future backend, not current passing
coverage. They must not run against a deployed unit. Complete Stage 0 in
[persistent-backlog.md](persistent-backlog.md) on a dedicated bench Opta first.

### Host codec and backend tests

- Golden vectors for every codec version, with exact expected bytes.
- Round trips for maximum-length strings, optional/invalid sensor groups,
  finite boundary values, and the documented non-finite-value policy.
- Rejection of unknown versions, invalid header/payload lengths, oversized
  fields, truncation at every byte, CRC mismatch, and trailing garbage.
- Queue ordering and stable sequence IDs across enqueue, peek, acknowledge,
  reopen, and wrap/compaction behavior.
- Full-queue behavior that preserves committed FIFO entries, rejects the new
  record, and increments an explicit counter.
- Interrupted-write simulation before, during, and after the enqueue commit
  point; no partial record may become visible.
- Interrupted acknowledgement simulation at every metadata update; recovery
  may retry an upload but may not silently lose an unacknowledged record.
- Corrupt tail, head, and interior records, including deterministic quarantine
  or stop behavior without silent reordering.

### Mount and reboot behavior

- Missing, invalid, overlapping, undersized, or unexpected partition 4 must
  fail closed without probing offset zero.
- LittleFS mount failure must not call format, erase, repartition, or any
  write-capable recovery path. Firmware must continue with
  `RamReadingStorage`, expose an actionable diagnostic, and keep sensor/upload
  operation available.
- A clean reboot must recover exactly the committed, unacknowledged records in
  FIFO order. Staging artifacts must be ignored.
- Reboots after accepted uploads and permanent-rejection acknowledgements must
  not resurrect an acknowledgement that was durably committed.

### Power-cut HIL gate

On the dedicated, canonically formatted bench Opta, automate repeated hard
power cuts at each instrumented enqueue and acknowledgement boundary. After
every boot, verify queue contents and ordering, CRC handling, fallback
diagnostics, and ability to drain accepted records. Fill the filesystem/queue
to capacity and repeat the cycle. Before and after the campaign, verify Wi-Fi
firmware/certificates and network operation, and prove from instrumentation
that no read/program/erase address escaped QSPI partition 4.

Passing host tests alone does not enable production persistence. The complete
Stage 0 evidence, mount-failure checks, reboot recovery, corruption/full-queue
tests, and power-cut HIL campaign require review before an experimental
environment may be created.
