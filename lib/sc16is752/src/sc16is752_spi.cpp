#include "sc16is752_spi.h"

#include <logger_core/sc16is752_codec.h>

namespace {

constexpr uint8_t REG_RHR_THR = 0x00;
constexpr uint8_t REG_IER = 0x01;
constexpr uint8_t REG_FCR = 0x02;
constexpr uint8_t REG_LCR = 0x03;
constexpr uint8_t REG_MCR = 0x04;
constexpr uint8_t REG_LSR = 0x05;
constexpr uint8_t REG_SPR = 0x07;
constexpr uint8_t REG_RXLVL = 0x09;

constexpr uint8_t LCR_DLAB = 0x80;
constexpr uint8_t MCR_LOOPBACK = 0x10;
constexpr uint8_t LSR_TRANSMITTER_EMPTY = 0x40;

}  // namespace

Sc16is752Spi::Sc16is752Spi(
    SPIClass& spi, int csPin, int irqPin, int channel1EnablePin,
    int channel2EnablePin, uint8_t transmitEnableLevel)
    : spi_(spi),
      settings_(SPI_HZ, MSBFIRST, SPI_MODE0),
      csPin_(csPin),
      irqPin_(irqPin),
      enablePins_{channel1EnablePin, channel2EnablePin},
      transmitEnableLevel_(transmitEnableLevel) {}

bool Sc16is752Spi::begin() {
  const uint8_t receiveLevel =
      transmitEnableLevel_ == HIGH ? LOW : HIGH;
  digitalWrite(csPin_, HIGH);
  pinMode(csPin_, OUTPUT);
  for (int pin : enablePins_) {
    if (pin < 0) continue;
    digitalWrite(pin, receiveLevel);
    pinMode(pin, OUTPUT);
  }
  if (irqPin_ >= 0) pinMode(irqPin_, INPUT_PULLUP);
  spi_.begin();

  constexpr uint8_t probeValues[] = {0x5A, 0xA5, 0x3C, 0xC3};
  started_ = true;
  for (uint8_t channel = 0; channel < 2 && started_; ++channel) {
    for (uint8_t probeValue : probeValues) {
      writeRegister(channel, REG_SPR, probeValue);
      if (readRegister(channel, REG_SPR) != probeValue) {
        started_ = false;
        break;
      }
    }
  }
  return started_;
}

bool Sc16is752Spi::configure(
    uint8_t channel, uint32_t baud, LineFormat format) {
  if (!started_ && !begin()) return false;
  if (!validChannel(channel) || baud == 0) return false;

  uint16_t divisor = 0;
  if (!logger_core::sc16is752BaudDivisor(
          CRYSTAL_HZ, baud, divisor)) {
    return false;
  }

  const uint8_t lineControl = logger_core::sc16is752LineControl(
      format == LineFormat::EIGHT_E_ONE);
  writeRegister(channel, REG_LCR, lineControl | LCR_DLAB);
  writeRegister(channel, 0x00, static_cast<uint8_t>(divisor));
  writeRegister(channel, 0x01, static_cast<uint8_t>(divisor >> 8U));
  writeRegister(channel, REG_LCR, lineControl);
  writeRegister(channel, REG_FCR, 0x07);
  writeRegister(channel, REG_FCR, 0x01);
  writeRegister(channel, REG_IER, 0x01);
  baud_[channel] = baud;
  setTransmit(channel, false);
  return true;
}

void Sc16is752Spi::clearReceive(
    uint8_t channel, uint32_t quietMs, uint32_t maximumMs) {
  if (!validChannel(channel)) return;
  const uint32_t startedMs = millis();
  uint32_t quietSinceMs = startedMs;
  while (millis() - quietSinceMs < quietMs &&
         millis() - startedMs < maximumMs) {
    while (available(channel) > 0) {
      read(channel);
      quietSinceMs = millis();
    }
  }
}

bool Sc16is752Spi::write(
    uint8_t channel, const uint8_t* data, size_t length,
    uint32_t preDelayUs, uint32_t postDelayMarginUs) {
  if (!started_ || !validChannel(channel) || data == nullptr ||
      length == 0 || length > FIFO_BYTES || baud_[channel] == 0) {
    return false;
  }
  if (readRegister(channel, 0x08) < length) {
    writeRegister(channel, REG_FCR, 0x05);
    writeRegister(channel, REG_FCR, 0x01);
    delay(1);
    if (readRegister(channel, 0x08) < length) return false;
  }

  setTransmit(channel, true);
  delayMicroseconds(preDelayUs);
  writeFifo(channel, data, length);

  const uint32_t startedUs = micros();
  const uint32_t maximumWaitUs =
      static_cast<uint32_t>(
          (length * 12ULL * 1000000ULL + baud_[channel] - 1U) /
          baud_[channel]) +
      postDelayMarginUs + 10000UL;
  while ((readRegister(channel, REG_LSR) & LSR_TRANSMITTER_EMPTY) == 0) {
    if (micros() - startedUs >= maximumWaitUs) {
      writeRegister(channel, REG_FCR, 0x05);
      writeRegister(channel, REG_FCR, 0x01);
      setTransmit(channel, false);
      return false;
    }
    delayMicroseconds(50);
  }
  delayMicroseconds(postDelayMarginUs);
  setTransmit(channel, false);
  return true;
}

int Sc16is752Spi::available(uint8_t channel) {
  if (!started_ || !validChannel(channel)) return 0;
  return readRegister(channel, REG_RXLVL);
}

int Sc16is752Spi::read(uint8_t channel) {
  if (available(channel) <= 0) return -1;
  return readRegister(channel, REG_RHR_THR);
}

int Sc16is752Spi::transmitAvailable(uint8_t channel) {
  if (!started_ || !validChannel(channel)) return 0;
  return readRegister(channel, 0x08);
}

uint8_t Sc16is752Spi::lineStatus(uint8_t channel) {
  if (!started_ || !validChannel(channel)) return 0;
  return readRegister(channel, REG_LSR);
}

bool Sc16is752Spi::internalLoopbackTest(
    uint8_t channel, uint32_t timeoutMs) {
  if (!started_ || !validChannel(channel) || baud_[channel] == 0 ||
      timeoutMs == 0) {
    return false;
  }

  constexpr uint8_t testBytes[] = {0x55, 0xAA, 0x00, 0xFF};
  clearReceive(channel, 2U, 20U);
  writeRegister(channel, REG_MCR, MCR_LOOPBACK);
  writeFifo(channel, testBytes, sizeof(testBytes));

  const uint32_t startedMs = millis();
  while (available(channel) < static_cast<int>(sizeof(testBytes)) &&
         millis() - startedMs < timeoutMs) {
    delay(1);
  }

  bool matched =
      available(channel) >= static_cast<int>(sizeof(testBytes));
  for (uint8_t expected : testBytes) {
    const int received = read(channel);
    if (received != expected) matched = false;
  }
  writeRegister(channel, REG_MCR, 0x00);
  clearReceive(channel, 2U, 20U);
  return matched;
}

bool Sc16is752Spi::interruptActive() const {
  return irqPin_ >= 0 && digitalRead(irqPin_) == LOW;
}

uint8_t Sc16is752Spi::command(
    uint8_t channel, uint8_t reg, bool read) const {
  return static_cast<uint8_t>(
      logger_core::sc16is752Command(channel, reg, read));
}

uint8_t Sc16is752Spi::readRegister(uint8_t channel, uint8_t reg) {
  spi_.beginTransaction(settings_);
  digitalWrite(csPin_, LOW);
  spi_.transfer(command(channel, reg, true));
  const uint8_t value = spi_.transfer(0xFF);
  digitalWrite(csPin_, HIGH);
  spi_.endTransaction();
  return value;
}

void Sc16is752Spi::writeRegister(
    uint8_t channel, uint8_t reg, uint8_t value) {
  spi_.beginTransaction(settings_);
  digitalWrite(csPin_, LOW);
  spi_.transfer(command(channel, reg, false));
  spi_.transfer(value);
  digitalWrite(csPin_, HIGH);
  spi_.endTransaction();
}

void Sc16is752Spi::writeFifo(
    uint8_t channel, const uint8_t* data, size_t length) {
  spi_.beginTransaction(settings_);
  digitalWrite(csPin_, LOW);
  spi_.transfer(command(channel, REG_RHR_THR, false));
  for (size_t i = 0; i < length; ++i) spi_.transfer(data[i]);
  digitalWrite(csPin_, HIGH);
  spi_.endTransaction();
}

void Sc16is752Spi::setTransmit(uint8_t channel, bool transmitting) {
  const int pin = enablePin(channel);
  if (pin < 0) return;
  const uint8_t receiveLevel =
      transmitEnableLevel_ == HIGH ? LOW : HIGH;
  digitalWrite(
      pin, transmitting ? transmitEnableLevel_ : receiveLevel);
}

int Sc16is752Spi::enablePin(uint8_t channel) const {
  return validChannel(channel) ? enablePins_[channel] : -1;
}

bool Sc16is752Spi::validChannel(uint8_t channel) const {
  return channel < 2;
}
