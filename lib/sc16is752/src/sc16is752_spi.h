#pragma once

#include <Arduino.h>
#include <SPI.h>

class Sc16is752Spi {
 public:
  enum class LineFormat : uint8_t {
    EIGHT_N_ONE,
    EIGHT_E_ONE,
  };

  Sc16is752Spi(
      SPIClass& spi, int csPin, int irqPin, int channel1EnablePin,
      int channel2EnablePin, uint8_t transmitEnableLevel = HIGH);

  bool begin();
  bool configure(uint8_t channel, uint32_t baud, LineFormat format);
  void clearReceive(uint8_t channel, uint32_t quietMs, uint32_t maximumMs);
  bool write(
      uint8_t channel, const uint8_t* data, size_t length,
      uint32_t preDelayUs, uint32_t postDelayMarginUs);
  int available(uint8_t channel);
  int read(uint8_t channel);
  int transmitAvailable(uint8_t channel);
  uint8_t lineStatus(uint8_t channel);
  bool internalLoopbackTest(uint8_t channel, uint32_t timeoutMs);
  bool interruptActive() const;

 private:
  static constexpr uint32_t CRYSTAL_HZ = 14745600UL;
  static constexpr uint32_t SPI_HZ = 1000000UL;
  static constexpr size_t FIFO_BYTES = 64;

  uint8_t command(uint8_t channel, uint8_t reg, bool read) const;
  uint8_t readRegister(uint8_t channel, uint8_t reg);
  void writeRegister(uint8_t channel, uint8_t reg, uint8_t value);
  void writeFifo(uint8_t channel, const uint8_t* data, size_t length);
  void setTransmit(uint8_t channel, bool transmitting);
  int enablePin(uint8_t channel) const;
  bool validChannel(uint8_t channel) const;

  SPIClass& spi_;
  SPISettings settings_;
  int csPin_;
  int irqPin_;
  int enablePins_[2];
  uint32_t baud_[2] = {};
  uint8_t transmitEnableLevel_;
  bool started_ = false;
};
