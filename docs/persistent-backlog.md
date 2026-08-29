# Persistent backlog: staged design and safety gate

## Status and scope

Persistent storage is **not enabled**. Production firmware continues to use
`RamReadingStorage`, so queued readings still do not survive reset or power
loss. This document defines the safe staging work and the prerequisites for a
later implementation; it does not claim to fix power-loss data loss.

No `opta-persistent` PlatformIO environment exists in this phase. It must not be
added until the backend exists, is fail-closed, and has passed the acceptance
gate below. When it is eventually added, it must be a separate, explicitly
selected environment and must never replace `opta` as the default.

## Verified storage risks

- The default Mbed KVStore/TDBStore path on Opta may select QSPI starting at
  offset zero. Using it without an explicit, verified partition can overwrite
  Wi-Fi firmware and certificates.
- The canonical Opta QSPI layout has a 7 MB user-data partition in MBR
  partition 4, but that layout exists only after the canonical `QSPIFormat`
  tool has created it. The partition table on deployed units is unknown.
- Internal flash has only two apparently free 128 KB sectors. Firmware growth,
  linker placement, bootloader/OTA behavior, and automatic address selection
  make those sectors unsafe for a production backlog.
- `SiteReading` contains Arduino `String` members, including nested weather
  fields. Its object representation contains process-local state and must not be
  copied to storage with `memcpy`, written as a raw struct, or restored by
  casting bytes to `SiteReading`.

### Prohibited approaches

Normal firmware must not:

- use the default KVStore, TDBStore, default block device, or an implicitly
  selected QSPI address;
- initialize, format, erase, repartition, or auto-format QSPI;
- fall back from a missing/invalid partition table to QSPI offset zero;
- use internal flash sectors for the backlog or rely on automatic flash address
  selection;
- persist raw C++/Arduino objects, pointers, `String` internals, padding, or
  native struct layout;
- mount a filesystem with “format on mount failure” behavior; or
- enable persistence through a normal production build flag or runtime default.

## Stage 0: establish a known QSPI layout

No partition diagnostic is added here. A firmware API that appears read-only
may still initialize a block device, metadata layer, or driver, and that
behavior has not been proven write-free for the deployed Opta core. Therefore
there is no approved on-device, non-destructive inspection procedure for a
deployed unit in this phase.

Use the Arduino Mbed Opta core's canonical
`STM32H747_System > QSPIFormat` example only on a dedicated bench Opta whose
QSPI contents may be destroyed. Treat the tool as destructive, read its prompts
and source for the exact installed core version, preserve any required Wi-Fi
firmware/certificates using the vendor-supported workflow, and select the
canonical Opta partition scheme that creates the 7 MB user-data partition 4.
Do not run `QSPIFormat` merely to inspect a field unit, and do not upload its
sketch to production hardware.

Before backend development, record as bench evidence:

1. Opta hardware revision, bootloader version, and Arduino Mbed Opta core
   version.
2. The exact `QSPIFormat` source/version used and its displayed final partition
   map.
3. Successful Wi-Fi operation after formatting/restoration.
4. Partition 4 start, length, erase/program geometry, and proof that all backend
   I/O remains within those bounds.

If those facts cannot be established, persistent storage remains blocked.

## Intended backend

The intended backend is LittleFS2 on the verified MBR partition 4 only. The
backend must construct a bounded block-device view from the validated partition
start and length; it must reject a missing, malformed, overlapping, undersized,
or unexpected partition instead of guessing or falling back.

Mount failure must leave the filesystem untouched and fall back to the existing
RAM queue with a visible diagnostic. Formatting/recovery that writes media must
be available only in a separate bench-maintenance workflow, never during normal
boot. The existing RAM backend remains the production default even after an
experimental backend is introduced.

## Record codec

The codec must serialize fields explicitly rather than serializing
`SiteReading` memory. Each record needs:

- fixed magic and codec version;
- header length and payload length;
- monotonic record/sequence identifier;
- explicit byte order and fixed-width integer encodings;
- an integrity CRC covering the immutable header fields and payload;
- bounded counts and bounded UTF-8 string fields;
- explicit presence/validity bits for optional sensor groups; and
- a documented float representation (fixed-width IEEE-754 bit encoding or
  scaled integers), including handling for non-finite values.

Decoding must validate all lengths before allocation, cap every field to a
compile-time maximum, reject truncated/oversized/unknown-version records, and
construct fresh `String` values from validated bytes. Compatibility tests must
use committed golden byte vectors so a compiler, core, or architecture change
cannot silently alter the format.

## Queue and recovery semantics

- **Enqueue:** append a complete record to a temporary/staging file, sync it,
  then publish it with a LittleFS atomic operation. A record is visible only
  after its commit point. Report success only after required metadata is synced.
- **FIFO identity:** committed records have stable sequence IDs. Recovery sorts
  and replays by those IDs, not directory enumeration order.
- **Peek/upload:** uploading never mutates the head record.
- **Acknowledge:** remove/mark the exact head sequence only after confirmed
  upload acceptance or the existing explicit permanent-rejection policy. Sync
  the acknowledgement before reporting the dequeue complete. A reset may cause
  a duplicate retry, but must not silently lose an unacknowledged record.
- **Recovery:** ignore uncommitted staging artifacts; validate magic, version,
  lengths, sequence, and CRC before exposing a record. A corrupt tail may be
  quarantined/truncated only by a tested recovery rule. Interior corruption
  must be diagnosed and must not reorder later records silently.
- **Full queue:** preserve already committed FIFO data and reject the new
  enqueue with an explicit full/drop counter. Never erase the oldest entry
  implicitly.
- **Clear:** retain the existing explicit confirmed-clear intent. The persistent
  implementation must make clear atomic/recoverable and report the count.

## Staged acceptance gate

Persistence may be implemented only after Stage 0 establishes a disposable,
known-good bench Opta with canonical QSPI partition 4. It may be enabled in an
explicit `opta-persistent` environment only after all of these pass:

1. Host codec golden-vector, bounds, version, truncation, and CRC tests.
2. Backend tests for full queue, interrupted enqueue, interrupted acknowledge,
   corrupt head/tail/interior records, and deterministic reboot recovery.
3. A mount-failure test proving no format/erase/write attempt and successful
   RAM fallback with diagnostics.
4. Instrumented proof that every read/program/erase address is inside verified
   partition 4.
5. Bench reboot and repeated hard power-cut HIL at each enqueue/ack commit
   boundary, with Wi-Fi firmware verified before and after.
6. A documented operator recovery/reformat procedure and review of generated
   firmware size/linker changes.

Until this evidence is reviewed, `robust-persistent-backlog` is blocked on the
dedicated bench Opta and verified canonical QSPI-P4 layout. Production remains
RAM-backed.
