#include "config.h"

#if defined(LOGGER_BOARD_GIGA)
#include "giga_board_config.h"

namespace {

uint32_t lastDetectionAttemptMs = 0;

bool detectionDue() {
  return lastDetectionAttemptMs == 0 ||
         millis() - lastDetectionAttemptMs >=
             GIGA_INKPLATE_REDETECT_INTERVAL_MS;
}

bool detectDisplay() {
  lastDetectionAttemptMs = millis();
  return initInkplateUartDisplay();
}

}  // namespace

bool initDisplay() {
  if (detectDisplay()) return true;
  displayPresent = false;
  displayAwake = false;
  Serial.println("No Inkplate detected; Giga display running headless");
  return false;
}

void wakeDisplayForTimeout() {
  wakeInkplateUartDisplay();
}

void updateDisplay() {
  if (!displayPresent) {
    if (detectionDue()) detectDisplay();
    return;
  }
  updateInkplateUartDisplay();
}

void sleepDisplay() {
  if (displayPresent) sleepInkplateUartDisplay();
}

String lastDisplayWakeRequestUtc() {
  return lastInkplateWakeRequestUtc();
}

String lastDisplayRefreshUtc() {
  return lastInkplateRefreshUtc();
}

String lastDisplayWakeRequestAge() {
  return lastInkplateWakeRequestAge();
}

String lastDisplayRefreshAge() {
  return lastInkplateRefreshAge();
}

uint32_t displayRefreshCount() {
  return inkplateRefreshCount();
}

uint32_t displayI2cRecoveryCount() {
  return 0;
}

const char* displayBackendName() {
  return displayPresent ? "inkplate_uart" : "headless";
}

String displayLastError() {
  return inkplateLastError();
}

uint32_t displayLinkFailureCount() {
  return inkplateLinkFailureCount();
}

bool runDisplayCommand(const char* command, String& response) {
  if (!displayPresent) {
    response = "Inkplate UART display not detected";
    return false;
  }
  return sendInkplateCommand(command, response);
}
#endif
