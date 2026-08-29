#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logger_core {

uint8_t sc16is752Command(uint8_t channel, uint8_t reg, bool read);
bool sc16is752BaudDivisor(
    uint32_t crystalHz, uint32_t baud, uint16_t& divisor);
uint8_t sc16is752LineControl(bool evenParity);

}  // namespace logger_core
