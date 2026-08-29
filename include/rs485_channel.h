#pragma once

#include <stddef.h>
#include <stdint.h>

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
};

Rs485Channel& solinstRs485Channel();
Rs485Channel& weatherRs485Channel();
