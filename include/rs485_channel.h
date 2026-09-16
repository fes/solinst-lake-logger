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
};

Rs485Channel& solinstRs485Channel();
Rs485Channel& weatherRs485Channel();
