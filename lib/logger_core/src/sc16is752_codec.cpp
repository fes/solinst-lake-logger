#include "logger_core/sc16is752_codec.h"

#include <limits.h>

namespace logger_core {

uint8_t sc16is752Command(uint8_t channel, uint8_t reg, bool read) {
  return static_cast<uint8_t>(
      (read ? 0x80U : 0x00U) | ((reg & 0x0FU) << 3U) |
      ((channel & 0x01U) << 1U));
}

bool sc16is752BaudDivisor(
    uint32_t crystalHz, uint32_t baud, uint16_t& divisor) {
  if (crystalHz == 0 || baud == 0) return false;
  const uint64_t rounded =
      (static_cast<uint64_t>(crystalHz) + baud * 8ULL) /
      (baud * 16ULL);
  if (rounded == 0 || rounded > UINT16_MAX) return false;
  divisor = static_cast<uint16_t>(rounded);
  return true;
}

uint8_t sc16is752LineControl(bool evenParity) {
  return evenParity ? 0x1B : 0x03;
}

}  // namespace logger_core
