#include "config.h"

#if defined(LOGGER_BOARD_GIGA)
#include "giga_board_config.h"

namespace {

enum class GigaDisplayBackend : uint8_t {
  LEGACY_EPAPER,
  INKPLATE_UART
};

GigaDisplayBackend activeBackend = GigaDisplayBackend::LEGACY_EPAPER;
uint32_t lastInkplateDetectionAttemptMs = 0;
bool legacyDisplayInitialized = false;

bool shouldRetryInkplateDetection() {
  return lastInkplateDetectionAttemptMs == 0 ||
         millis() - lastInkplateDetectionAttemptMs >=
             GIGA_INKPLATE_REDETECT_INTERVAL_MS;
}

bool switchToInkplateIfPresent() {
  lastInkplateDetectionAttemptMs = millis();
  if (!initInkplateUartDisplay()) return false;
  if (legacyDisplayInitialized) {
    sleepLegacyEpaperDisplay();
  }
  activeBackend = GigaDisplayBackend::INKPLATE_UART;
  return true;
}

void switchToLegacyDisplay() {
  activeBackend = GigaDisplayBackend::LEGACY_EPAPER;
  if (!legacyDisplayInitialized || !displayPresent) {
    legacyDisplayInitialized = initLegacyEpaperDisplay();
  }
}

}  // namespace

bool initDisplay() {
  if (switchToInkplateIfPresent()) return true;
  switchToLegacyDisplay();
  return legacyDisplayInitialized;
}

void wakeDisplayForTimeout() {
  if (activeBackend == GigaDisplayBackend::INKPLATE_UART) {
    wakeInkplateUartDisplay();
  } else {
    wakeLegacyEpaperDisplay();
  }
}

void updateDisplay() {
  if (activeBackend == GigaDisplayBackend::LEGACY_EPAPER &&
      shouldRetryInkplateDetection() && switchToInkplateIfPresent()) {
    updateInkplateUartDisplay();
    return;
  }
  if (activeBackend == GigaDisplayBackend::INKPLATE_UART) {
    updateInkplateUartDisplay();
    if (!displayPresent) {
      switchToLegacyDisplay();
    }
  } else {
    updateLegacyEpaperDisplay();
  }
}

void sleepDisplay() {
  if (activeBackend == GigaDisplayBackend::INKPLATE_UART) {
    sleepInkplateUartDisplay();
  } else {
    sleepLegacyEpaperDisplay();
  }
}

String lastDisplayWakeRequestUtc() {
  return activeBackend == GigaDisplayBackend::INKPLATE_UART
             ? lastInkplateWakeRequestUtc()
             : lastLegacyEpaperWakeRequestUtc();
}

String lastDisplayRefreshUtc() {
  return activeBackend == GigaDisplayBackend::INKPLATE_UART
             ? lastInkplateRefreshUtc()
             : lastLegacyEpaperRefreshUtc();
}

String lastDisplayWakeRequestAge() {
  return activeBackend == GigaDisplayBackend::INKPLATE_UART
             ? lastInkplateWakeRequestAge()
             : lastLegacyEpaperWakeRequestAge();
}

String lastDisplayRefreshAge() {
  return activeBackend == GigaDisplayBackend::INKPLATE_UART
             ? lastInkplateRefreshAge()
             : lastLegacyEpaperRefreshAge();
}

uint32_t displayRefreshCount() {
  return activeBackend == GigaDisplayBackend::INKPLATE_UART
             ? inkplateRefreshCount()
             : legacyEpaperRefreshCount();
}

uint32_t displayI2cRecoveryCount() {
  return activeBackend == GigaDisplayBackend::INKPLATE_UART
             ? 0
             : legacyEpaperRecoveryCount();
}

const char* displayBackendName() {
  return activeBackend == GigaDisplayBackend::INKPLATE_UART
             ? "inkplate_uart"
             : "legacy_epaper";
}

String displayLastError() {
  return inkplateLastError();
}

uint32_t displayLinkFailureCount() {
  return inkplateLinkFailureCount();
}

bool runDisplayCommand(const char* command, String& response) {
  if (activeBackend == GigaDisplayBackend::INKPLATE_UART) {
    return sendInkplateCommand(command, response);
  }
  if (strcmp(command, "refresh") == 0) {
    wakeLegacyEpaperDisplay();
    response = "legacy refresh scheduled";
    return true;
  }
  response = "command requires Inkplate UART display";
  return false;
}
#endif
