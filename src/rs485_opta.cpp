#include "config.h"

#if defined(LOGGER_BOARD_OPTA)
namespace {

class OptaRs485Channel final : public Rs485Channel {
 public:
  const char* name() const override {
    return "Opta RS-485";
  }

  bool begin(uint32_t baud, uint16_t serialConfig, uint32_t preDelayUs,
             uint32_t postDelayMarginUs) override {
    if (started_ && baud_ == baud && serialConfig_ == serialConfig) return true;

    RS485.end();
    delay(50);
    const uint32_t postDelayUs =
        ((12000000UL + baud - 1U) / baud) + postDelayMarginUs;
    RS485.setDelays(preDelayUs, postDelayUs);
    RS485.begin(baud, serialConfig);
    RS485.receive();
    started_ = true;
    baud_ = baud;
    serialConfig_ = serialConfig;
    return true;
  }

  void clearReceive(uint32_t quietMs, uint32_t maximumMs) override {
    const uint32_t startedMs = millis();
    uint32_t quietSinceMs = startedMs;
    while (millis() - quietSinceMs < quietMs &&
           millis() - startedMs < maximumMs) {
      while (RS485.available() > 0) {
        RS485.read();
        quietSinceMs = millis();
      }
    }
  }

  bool write(const uint8_t* data, size_t length) override {
    if (data == nullptr || length == 0) return false;
    RS485.beginTransmission();
    const size_t written = RS485.write(data, length);
    RS485.endTransmission();
    RS485.receive();
    return written == length;
  }

  int available() override {
    return RS485.available();
  }

  int read() override {
    return RS485.read();
  }

 private:
  bool started_ = false;
  uint32_t baud_ = 0;
  uint16_t serialConfig_ = 0;
};

OptaRs485Channel optaChannel;

}  // namespace

Rs485Channel& solinstRs485Channel() {
  return optaChannel;
}

Rs485Channel& weatherRs485Channel() {
  return optaChannel;
}
#endif
