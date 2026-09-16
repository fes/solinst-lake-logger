#pragma once

#include <stddef.h>
#include <stdint.h>

// Passive liveliness/health probe for the channel's underlying UART
// hardware, distinct from whether the far-end Modbus device answered.
// This lets us tell "the bridge chip/transceiver itself is fine, the
// downstream sensor just isn't responding" apart from "the bridge/line
// itself is faulted (framing/parity/overrun/break)", which otherwise look
// identical from the Modbus layer as a plain timeout.
//
// Boards without a discrete bridge chip to interrogate (e.g. the Opta's
// built-in RS-485 transceiver) report `supported = false` rather than
// fabricating a false-positive/negative reading.
struct Rs485ChannelHealth {
  bool supported = false;
  uint8_t lineStatusRegister = 0;
  bool overrunError = false;
  bool parityError = false;
  bool framingError = false;
  bool breakDetected = false;
};

class Rs485Channel {
 public:
  virtual ~Rs485Channel() = default;
  virtual const char* name() const = 0;
  virtual bool begin(uint32_t baud, uint16_t serialConfig,
                     uint32_t preDelayUs, uint32_t postDelayMarginUs) = 0;
  virtual void clearReceive(uint32_t quietMs, uint32_t maximumMs) = 0;
  virtual bool write(const uint8_t* data, size_t length) = 0;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual Rs485ChannelHealth health() { return Rs485ChannelHealth{}; }

  // Attempt an automatic hardware-level recovery after repeated failures
  // on this channel (e.g. a full SC16IS752 SPI bridge re-init, including
  // its scratch-register self-test on both channels). This is deliberately
  // heavier-weight than the per-transaction begin()/configure() reset, and
  // is meant to be tried only after several consecutive failures, since it
  // can briefly disrupt the *other* channel sharing the same bridge chip.
  // Returns true if the recovery's own self-check passed. Boards with
  // nothing extra to reset (e.g. Opta's built-in RS-485 transceiver)
  // report false/not-supported; callers should treat that as "no
  // additional recovery available" rather than a failure needing escalation.
  virtual bool attemptRecovery() { return false; }

  // On-demand internal loopback self-test: writes known bytes and reads
  // them back over the bridge's UART core without touching the physical
  // RS-485 pair, to distinguish "the bridge/UART itself is broken" from
  // "the downstream sensor/cable is the problem" -- callable manually
  // (e.g. from the mobile diagnostics app) without waiting for a real
  // Modbus failure to happen first. This is disruptive to any in-flight
  // transaction on the channel, so callers should only invoke it between
  // probe cycles. Boards without bridge hardware to loop back report
  // false/not-supported.
  virtual bool selfTest(uint32_t timeoutMs) { return false; }
};

Rs485Channel& solinstRs485Channel();
Rs485Channel& weatherRs485Channel();
