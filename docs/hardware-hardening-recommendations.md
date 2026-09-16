# Hardware hardening recommendations: RS-485, power, and grounding

## Status and scope

This is a decision-support document, not an implementation spec. Nothing in
this file is required or scheduled; it exists so the physical rewiring/
enclosure layout choices can be evaluated offline before the next time the
box is opened up. Firmware changes referenced here as "already done" have
landed; everything else is a proposal pending a decision.

## Background: what prompted this

The Giga site logger (Solinst 301 water-level probe + DFRobot SEN0657
weather station, both RS-485/Modbus, sharing a single SPI-connected
SC16IS752 dual-UART bridge on the Waveshare 2-CH RS-485 HAT) had at least
one multi-day period where one or both sensors intermittently failed to
respond, coinciding with heavy rain, with intermittent (not total) readings
during the event and full self-recovery afterward with no lasting damage.
Both the electronics enclosure and the sensors themselves are watertight.

Working theory, in order of likelihood:

1. **Moisture/condensation inside the enclosure or at a cable
   entry/gland**, triggered by a rapid temperature drop during rain (a
   sealed box can still hit its internal dew point), causing a transient
   partial short or leakage path that dries out afterward. Matches: no
   battery voltage drop observed (checked the upload spreadsheet -- stayed
   above ~80% throughout), full self-recovery, no permanent damage.
2. **Ground potential shift from wet soil/dock structure**, injecting
   common-mode noise onto the RS-485 pair without any voltage-monitor-
   visible symptom.
3. Simple battery brownout of the shared SC16IS752 bridge chip -- **mostly
   ruled out** by the battery-voltage spreadsheet check (never dropped
   below ~80%), but kept as a residual possibility since solar input
   voltage during active cloud cover wasn't separately checked.

We cannot fully diagnose the original event after the fact -- the firmware's
Modbus failure history is only an 8-entry RAM ring buffer, and it wasn't yet
capturing power/bridge-health context when this happened. The firmware
changes below close that gap for next time, and don't depend on any of the
hardware changes proposed further down.

## Firmware diagnostics already implemented (as of `e0f4057`)

These are done, built, tested (45/45 native unit tests, `giga` and `opta`
firmware both compile clean), and pushed to `origin/main`. Listed here only
as context for the hardware proposals below.

- Every recorded Modbus failure (`ModbusFailureDiagnostic`, `include/config.h`)
  now also snapshots battery/solar voltage, battery charge %, solar charging
  state, and the SC16IS752's line-status register (overrun/parity/framing/
  break bits) at the moment of failure.
- `Rs485Channel` gained `health()` (passive line-status read), `selfTest()`
  (internal loopback test, exercises the bridge/UART core without touching
  the physical RS-485 pair), and `attemptRecovery()` (forces a full bridge
  re-init + scratch-register self-test). Opta's built-in transceiver has no
  equivalent hardware, so it reports these as unsupported rather than a
  false positive/negative.
- After 3 consecutive failures on either channel, the firmware now
  automatically attempts a full SC16IS752 bridge recovery
  (`MODBUS_BRIDGE_RECOVERY_THRESHOLD` in `include/config.h`).
- `POST /rs485/selftest` runs the on-demand loopback self-test on both
  channels (e.g. triggerable from the mobile diagnostics app), returning
  which channel(s) support/pass it.
- `/status` reports live bridge health continuously (not just at failure
  time), plus the new consecutive-failure and recovery counters.
- Every upload now also includes ~13 new extension fields (battery/solar
  state and per-channel bridge health bits at upload time) via
  `feslabs-web`'s existing lake-ingest extension-field mechanism, so this
  is now visible in the spreadsheet going forward, not just live via
  `/status`.

## 1. Waveshare isolated RS-485 converters (planned hardware swap)

**Update -- there is a better option than a separate converter module.**
Waveshare also sells a **2-Channel Isolated RS485 Expansion HAT**
(SC16IS752 + SP3485 solution) that is functionally a drop-in replacement
for the exact HAT already in the box, with isolation and protection added
onto the same board:

- Same SC16IS752 dual-UART bridge chip, same SPI interface -- `lib/sc16is752`
  and all of the health/recovery/self-test firmware landed this session
  keep working completely unchanged. This is not "Option A vs Option B"
  below; it *is* Option A, just with isolation built in at the source
  instead of bolted on with separate converter modules.
- Full galvanic isolation: **B0505LS isolated DC-DC** (isolates the RS-485
  side's power) plus an **ADUM1412 digital isolator** (isolates the SPI/
  logic-level signals) between the Giga/SC16IS752 side and the SP3485
  transceiver/field-wiring side. This solves the ground-potential-shift
  theory the same way the standalone converters would.
- **SMAJ12CA TVS diode array** for surge/lightning protection, plus a
  resettable fuse for over-current/over-voltage -- comparable protection
  to the standalone converter's 600W/15kV ESD spec.
- Same physical DIP-switch-selectable 120-ohm termination and TX/RX mode
  selection as the current HAT, so no change to the existing wiring
  conventions noted in `docs/giga-site-logger-architecture-notes.md`.
- It's a Raspberry Pi 40-pin HAT form factor, same as the current board;
  since the current board is already wired to the Giga via breakout SPI
  pins rather than the Pi header, the same approach should carry over, but
  physically confirm pin compatibility (CS/IRQ/EN pin locations) against
  the current board before ordering.

**Recommendation: prefer this isolated HAT over separate discrete
converter modules.** It gets the galvanic isolation and surge protection
we want, with zero firmware changes and zero UART budget impact (still
SPI-based), whereas the standalone TTL-to-RS485 converters below either
keep the SC16IS752 (redundant with what this HAT already provides) or
remove it and consume the Giga's scarce native UARTs (see the UART budget
problem under Option B below, which this isolated HAT avoids entirely by
not needing native UARTs at all).

The discrete-converter research below is kept for reference in case the
isolated HAT turns out not to be physically compatible with the current
wiring, or a future design wants to move off SPI/SC16IS752 entirely for
other reasons.

Researched: the **Waveshare Rail-Mount TTL-to-RS485 Isolated Converter**
(multi-isolation, SKU 23778 family) is electrically "dumb" -- there is no
SPI/I2C register interface to query, unlike the SC16IS752. What it provides
instead:

- Full galvanic isolation between the TTL (logic) side and the RS-485
  (field) side, including isolated power for the RS-485 side. This is the
  most direct hardware answer to the ground-potential-shift theory above --
  it breaks the shared-ground path between the logger's internal battery
  ground and each sensor's field wiring, without needing to touch earth
  ground at all (see the grounding section below).
- 600W lightning/surge protection and 15kV ESD protection on the RS-485
  line.
- A resettable PTC fuse, which is itself a form of free automatic recovery:
  it clears on its own once an overcurrent fault condition goes away, no
  firmware involvement needed.
- A physical DIP-switch-selectable 120-ohm termination resistor (make sure
  it's only enabled at the true end of each bus run, not on both ends or
  mid-bus).
- No software-visible telemetry: no error counters, no self-test, no status
  register. Any given unit's health can only be inferred indirectly (its
  LEDs, if present, are a manual visual check only) or via a current-sense
  monitor we add ourselves.

**Two wiring options, with different diagnostic consequences:**

- **Option A -- keep the SC16IS752 bridge, feed its TTL output into the new
  isolated converters** (converters replace only the transceiver/isolation
  stage). All of the health/recovery/self-test work above stays exactly as
  useful, since it reads the UART core upstream of the converter. Lowest
  firmware-risk option.
- **Option B -- remove the SC16IS752 entirely, use the Giga's native
  hardware UARTs directly into the converters.** Removes the shared-bridge-
  chip single point of failure (today, a bridge-level fault can affect both
  channels at once). Loses the SC16IS752-specific health/recovery/self-test
  primitives; would need new code reading the Giga's own UART peripheral
  error flags (mbed exposes framing/overrun status) to get equivalent
  visibility. More firmware work, but a structurally simpler/more robust
  design longer-term. **UART budget problem:** the Giga R1 only exposes
  3 hardware UARTs total (`Serial1`/`Serial2`/`Serial3` -- `Serial` itself
  is native USB, not a header UART), and `Serial1` is already committed to
  the Inkplate display link. That leaves only 2 free UARTs. Using both for
  Solinst + weather RS-485 leaves **zero** free for the Victron VE.Direct
  link (section 3), which also needs a dedicated UART. Option B is only
  viable alongside VE.Direct if a third UART source is added some other
  way (e.g. a second small UART-over-SPI/I2C bridge chip just for
  VE.Direct, similar in spirit to the SC16IS752 it would be replacing
  elsewhere), which adds back complexity in a different place.

**This UART constraint is a real argument for Option A**: keeping the
SC16IS752 for both RS-485 channels leaves both native UARTs free -- one
for VE.Direct, one spare -- with no extra bridge chip needed anywhere.

**Optional add-on either way:** rather than one INA219/INA228 per rail,
a single **TI INA3221** covers this in one part -- it's a 3-channel
high-side current/voltage monitor on one I2C chip, each channel
independently 0-26V with its own programmable critical/warning alert
output, and 4 selectable I2C addresses if more than 3 channels are ever
needed (stack a second chip). One INA3221 could cover both converters
plus a spare channel (e.g. the display) in a single part, instead of
three separate INA219/228 boards. Tradeoff versus the INA228 already used
for battery/solar: 13-bit resolution (fine for "is this rail alive and
drawing a sane current," not as precise for metering) and no built-in
energy/charge accumulation register, so any Wh/Ah totals would still need
to be computed in firmware from repeated voltage x current samples.

**Open questions before committing to a wiring plan:**
- Option A or B? Given the UART budget above, Option A is the pragmatic
  default unless there's a strong reason to eliminate the shared bridge.
- Is per-channel current-sensing worth the added parts/complexity for this
  deployment, or is that over-engineering for a single dock?
- If added, one INA3221 (Solinst converter + weather converter, one channel
  spare) is likely enough for this; a second chip (different I2C address)
  would only be needed if switched power (section 4) grows beyond 3 rails.

## 2. Grounding: decision made -- no earth/lake-bed ground

**Decided: not doing this.** Recorded here only so the reasoning is
preserved. Docks and marinas have specific code (NEC Article 555) around
grounding precisely because of Electric Shock Drowning (ESD) risk -- a
driven ground rod or wire into the water can, under a fault condition
elsewhere, energize the water and injure or kill someone swimming nearby.
Code-compliant dock grounding uses equipotential bonding back to a proper
grounding electrode system, and is an electrician's job, not a DIY project,
if ever wanted for lightning protection.

The ground-potential-shift concern this would have addressed is instead
handled by the isolated RS-485 converters above (galvanic isolation avoids
needing a shared ground reference at all), which is both safer and
sufficient for the actual problem.

## 3. MPPT charge controller: load-isolation breaks our charge-state math

**Model confirmed: Victron Energy SmartSolar MPPT 75/15 (12/24V, 15A).**
Good news -- no need to change models. This unit has both a load output
terminal (so the isolated-load concern below is real and applies) and a
**VE.Direct port**, which solves the problem better than any voltage/current
heuristic could:

- VE.Direct is a 3.3V TTL UART, 19200 baud 8N1, distinct from Modbus but
  just as easy to consume from firmware. About once per second the
  controller streams a plain-text frame of `LABEL\tvalue` lines terminated
  by a checksum, e.g. `V` (battery voltage, mV), `I` (battery current, mA,
  signed), `VPV`/`PPV` (panel voltage/power), `CS` (charge state: e.g.
  0=Off, 3=Bulk, 4=Absorption, 5=Float, 7=Equalize, 2=Fault), and an error
  code field. Full frame spec: Victron's public
  [VE.Direct Protocol PDF](https://www.victronenergy.com/upload/documents/VE.Direct-Protocol.pdf).
  Existing open-source Arduino parsers (e.g. `winginitau/VictronVEDirectArduino`)
  are usable as a reference/starting point.
- Victron does **not** put Modbus on the charge controller itself --
  Modbus TCP is only exposed via a separate Cerbo/Venus GX gateway device,
  which would be a needless extra box and internet/GX ecosystem dependency
  for this single-controller deployment. VE.Direct is the right interface
  here, not a reason to look at a different model.
- Practically: wire the controller's VE.Direct TX (and ideally RX, for
  future write access e.g. remote on/off) into a free Giga UART. Check
  whether the Giga's UART pins tolerate 3.3V-to-3.3V directly (they should,
  since the Giga itself is 3.3V logic) before skipping a level shifter --
  confirm against both the Giga R1 and VE.Direct datasheets rather than
  assuming.
- Once parsed, `CS` and the error field should **replace**
  `batteryChargePercent()`/`solarCharging()` outright for any system that
  has this controller, rather than trying to patch the voltage-based
  heuristics -- the controller already knows its own true charge state and
  our guesses can only be worse than that.

The load-output topology question below still matters for the physical
INA228 wiring, independent of adding VE.Direct:

Current logic (`lib/logger_core/src/domain_logic.cpp`):

```cpp
float batteryChargePercent(bool voltageValid, float voltageV) {
  // linear map: 12.0V -> 0%, 13.4V -> 100%
}
bool solarCharging(bool currentValid, float currentA) {
  return currentValid && isfinite(currentA) && currentA > 0.05f;
}
```

Both assumptions were written for a topology where the battery and load
share one electrical node, and solar current flowing implies the battery is
gaining charge. If the new MPPT controller has an isolated load output
(feeding the load directly from the panel during daylight, independent of
the battery), that breaks down:

- If the `battery_output` INA228 is wired at the controller's **load**
  output rather than true battery terminals, its voltage may reflect a
  regulated load rail rather than actual battery state of charge.
- Solar current flowing no longer implies the battery itself is charging --
  the controller can satisfy load demand straight from the panel while the
  battery sits idle, or even still discharges to cover a shortfall, and our
  two-monitor setup (battery_output + solar_input) cannot currently
  distinguish that split.

**What's needed to fix this properly:**
1. Confirm exactly where each existing INA228 shunt sits relative to the
   controller's PV / BATT / LOAD ports.
2. ~~Identify the MPPT controller model~~ -- done: Victron SmartSolar
   MPPT 75/15, which has VE.Direct telemetry (see above). Recommendation
   is to add the VE.Direct parser and use its `CS`/error fields as the
   authoritative charge state, rather than adding a third INA228 purely to
   patch the voltage heuristic.
3. A third INA228 at the true battery terminals is still worth adding only
   if we specifically want independent current draw of load vs. controller
   for power-budgeting/diagnostics beyond charge state itself -- optional,
   not required once VE.Direct is in place.

## 4. Individually switched power for converters/controllers/displays/sensors

Proposal, not yet designed in detail. Recommendation: **solid-state load
switches (MOSFET-based, e.g. a P-channel MOSFET high-side switch or an
integrated load-switch IC), not electromechanical relays** -- no coil
current draw (matters on a solar/battery budget), no moving parts to
corrode in a marine environment, cleaner/faster switching, trivially driven
from a Giga GPIO.

**Where this would help:**
- A true hardware power-cycle for the RS-485 bridge/converters, as a
  second escalation tier above the software-only recovery already
  implemented (`Rs485Channel::attemptRecovery()`) -- useful if a chip or
  converter is genuinely wedged rather than just confused.
- Fault isolation for troubleshooting: remotely cut power to just the
  weather station channel and see whether the Solinst channel behaves
  normally, directly testing a suspected fault on one sensor's wiring
  without a truck trip.
- Power budget management: switch off the display (beyond its existing
  sleep command) or a channel that's not currently needed during an
  extended low-solar stretch.

**Design constraints to carry into any layout:**
- Switching transients on a shared bus could inject noise into a still-
  active channel -- sequence any switch-off by first forcing DE/RE to a
  safe receive state, and add a brief settle delay before re-enabling.
- Default-safe: each switch must default to "on" via a pull resistor (not
  an actively-driven default-off GPIO), so a Giga crash or watchdog reset
  never leaves a sensor permanently unpowered.
- The SC16IS752 bridge (if kept, see Option A above) still serves both
  channels, so power-cycling it affects both channels at once, same
  limitation as the existing software-only recovery.
- Confirm the Solinst 301 and DFRobot weather station don't need unusual
  settling time after a cold power-up before answering Modbus queries --
  the existing retry/backoff loop should absorb ordinary cases, but worth
  checking against each sensor's datasheet.

**Open questions:**
- How many free GPIOs will be available on the Giga once the RS-485
  converter wiring (and any current-sense additions from section 1) is
  finalized?
- Which loads are actually worth switching -- bridge/converters and
  display seem clearly worthwhile; the sensors themselves may not be, since
  they're the thing we're trying to keep observable.
- Is this worth doing in the same physical rewiring pass as the isolated
  converters, or as a separate follow-up once that's proven out?
