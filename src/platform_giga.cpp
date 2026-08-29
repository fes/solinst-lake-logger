#include "config.h"
#include "giga_board_config.h"
#include "runtime_boundaries.h"

#if defined(LOGGER_BOARD_GIGA)
namespace {

void beginGigaPlatform() {
  const uint8_t rs485ReceiveLevel =
      GIGA_RS485_TRANSMIT_ENABLE_LEVEL == HIGH ? LOW : HIGH;
  digitalWrite(GIGA_RS485_CHANNEL1_ENABLE_PIN, rs485ReceiveLevel);
  pinMode(GIGA_RS485_CHANNEL1_ENABLE_PIN, OUTPUT);
  digitalWrite(GIGA_RS485_CHANNEL2_ENABLE_PIN, rs485ReceiveLevel);
  pinMode(GIGA_RS485_CHANNEL2_ENABLE_PIN, OUTPUT);
  digitalWrite(GIGA_RS485_CS_PIN, HIGH);
  pinMode(GIGA_RS485_CS_PIN, OUTPUT);
  digitalWrite(GIGA_EPAPER_CS_PIN, HIGH);
  pinMode(GIGA_EPAPER_CS_PIN, OUTPUT);
  if (GIGA_EPAPER_ENABLED) {
    digitalWrite(
        GIGA_EPAPER_POWER_PIN, GIGA_EPAPER_POWER_ENABLE_LEVEL);
    pinMode(GIGA_EPAPER_POWER_PIN, OUTPUT);
  }
  SPI1.begin();
  initUserInterface();
  Serial.println("Giga platform initialized in single-core M7 mode");
}

void handleGigaInput() {
  handleUserButton();
}

void tickGigaPlatform() {
  updateUserInterface();
}

bool beginGigaDisplay() {
  if (!GIGA_EPAPER_ENABLED) {
    Serial.println("Giga e-paper disabled; running headless");
  }
  return initDisplay();
}

void tickGigaDisplay() {
  updateDisplay();
}

void beginGigaAuxiliarySensors() {
  initPowerMonitors();
  initWeatherSensor();
}

void pollGigaAuxiliarySensors(bool force) {
  pollWeatherIfDue(force);
}

}  // namespace

const PlatformBoundary ACTIVE_PLATFORM = {
    beginGigaPlatform,
    handleGigaInput,
    tickGigaPlatform};

const DisplayBoundary ACTIVE_DISPLAY = {
    beginGigaDisplay,
    tickGigaDisplay};

const AuxiliarySensorBoundary ACTIVE_AUXILIARY_SENSORS = {
    beginGigaAuxiliarySensors,
    pollPowerMonitorsIfDue,
    pollGigaAuxiliarySensors};
#endif
