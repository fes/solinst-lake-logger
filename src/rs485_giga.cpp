#include "config.h"
#include "giga_board_config.h"
#include <sc16is752_spi.h>

#if defined(LOGGER_BOARD_GIGA)
namespace {

class GigaRs485Channel final : public Rs485Channel {
 public:
  GigaRs485Channel(
      const char* channelName, Sc16is752Spi& bridge, uint8_t channel)
      : channelName_(channelName),
        bridge_(bridge),
        channel_(channel) {}

  const char* name() const override {
    return channelName_;
  }

  bool begin(uint32_t baud, uint16_t serialConfig, uint32_t preDelayUs,
             uint32_t postDelayMarginUs) override {
    preDelayUs_ = preDelayUs;
    postDelayMarginUs_ = postDelayMarginUs;
    Sc16is752Spi::LineFormat format;
    if (serialConfig == SERIAL_8N1) {
      format = Sc16is752Spi::LineFormat::EIGHT_N_ONE;
    } else if (serialConfig == SERIAL_8E1) {
      format = Sc16is752Spi::LineFormat::EIGHT_E_ONE;
    } else {
      return false;
    }
    // Reapply channel configuration before every Modbus transaction. This
    // matches the HIL path and resets the FIFOs so a transient SC16IS752
    // TX-full condition cannot poison later requests.
    if (!bridge_.configure(channel_, baud, format)) return false;
    return true;
  }

  void clearReceive(uint32_t quietMs, uint32_t maximumMs) override {
    bridge_.clearReceive(channel_, quietMs, maximumMs);
  }

  bool write(const uint8_t* data, size_t length) override {
    if (data == nullptr || length == 0) return false;
    return bridge_.write(
        channel_, data, length, preDelayUs_, postDelayMarginUs_);
  }

  int available() override { return bridge_.available(channel_); }

  int read() override { return bridge_.read(channel_); }

 private:
  const char* channelName_;
  Sc16is752Spi& bridge_;
  uint8_t channel_;
  uint32_t preDelayUs_ = 0;
  uint32_t postDelayMarginUs_ = 0;
};

Sc16is752Spi bridge(
    SPI1, GIGA_RS485_CS_PIN, GIGA_RS485_IRQ_PIN,
    GIGA_RS485_CHANNEL1_ENABLE_PIN, GIGA_RS485_CHANNEL2_ENABLE_PIN,
    GIGA_RS485_TRANSMIT_ENABLE_LEVEL);
GigaRs485Channel solinstChannel(
    "Giga SC16IS752 channel 1", bridge, 0);
GigaRs485Channel weatherChannel(
    "Giga SC16IS752 channel 2", bridge, 1);

}  // namespace

Rs485Channel& solinstRs485Channel() {
  return solinstChannel;
}

Rs485Channel& weatherRs485Channel() {
  return weatherChannel;
}
#endif
