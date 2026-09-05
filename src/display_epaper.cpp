#include "config.h"

#if defined(LOGGER_BOARD_GIGA)
#include <GxEPD2_BW.h>
#include <SPI.h>
#include <stdlib.h>

#include <gdeq0426t82_driver.h>
#include "giga_board_config.h"

namespace {

constexpr uint16_t EPAPER_PAGE_HEIGHT = 40;
constexpr uint32_t DISPLAY_RETRY_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr uint16_t EPAPER_BLACK = GxEPD_BLACK;
constexpr uint16_t EPAPER_WHITE = GxEPD_WHITE;
static_assert(
    Gdeq0426t82Driver::WIDTH * EPAPER_PAGE_HEIGHT / 8U <= 4096U,
    "E-paper page buffer exceeds the Giga memory budget");

GxEPD2_BW<Gdeq0426t82Driver, EPAPER_PAGE_HEIGHT> epaper(
    Gdeq0426t82Driver(
        GIGA_EPAPER_CS_PIN, GIGA_EPAPER_DC_PIN, GIGA_EPAPER_RST_PIN,
        GIGA_EPAPER_BUSY_PIN));

logger_core::EpaperRefreshState refreshState;
bool displayRefreshPending = false;
bool displayQuietWindowActive = false;
bool displayIsWhite = false;
uint32_t nextDisplayAttemptMs = 0;
uint32_t displayRefreshCountLocal = 0;
uint32_t displayRecoveryCountLocal = 0;
uint32_t lastDisplayWakeRequestMs = 0;
uint32_t lastDisplayRefreshMs = 0;
String lastDisplayWakeRequestUtcValue;
String lastDisplayRefreshUtcValue;

void formatAge(bool valid, uint32_t ageSeconds, char* output, size_t length) {
  if (!valid) {
    snprintf(output, length, "never");
    return;
  }
  const uint32_t hours = ageSeconds / 3600U;
  const uint32_t minutes = (ageSeconds % 3600U) / 60U;
  if (hours > 0) {
    snprintf(output, length, "%luh %lum", static_cast<unsigned long>(hours),
             static_cast<unsigned long>(minutes));
  } else {
    snprintf(output, length, "%lum", static_cast<unsigned long>(minutes));
  }
}

void printAt(int16_t x, int16_t y, uint8_t size, const char* text) {
  epaper.setTextSize(size);
  epaper.setCursor(x, y);
  epaper.print(text);
}

void printRightAligned(
    int16_t right, int16_t y, uint8_t size, const char* text) {
  const int16_t width = static_cast<int16_t>(strlen(text) * 6U * size);
  printAt(right - width, y, size, text);
}

void powerCycleDisplayController() {
  pinMode(GIGA_EPAPER_POWER_PIN, OUTPUT);
  digitalWrite(
      GIGA_EPAPER_POWER_PIN,
      GIGA_EPAPER_POWER_ENABLE_LEVEL == HIGH ? LOW : HIGH);
  delay(100);
  digitalWrite(
      GIGA_EPAPER_POWER_PIN, GIGA_EPAPER_POWER_ENABLE_LEVEL);
  delay(100);
}

void initializeDisplayDriver(bool initial) {
  pinMode(GIGA_EPAPER_BUSY_PIN, INPUT);
  epaper.epd2.selectSPI(
      SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  epaper.init(115200, initial, 10, false);
  pinMode(GIGA_EPAPER_BUSY_PIN, INPUT_PULLDOWN);
  epaper.setRotation(0);
  epaper.setTextWrap(false);
}

void drawWidget(int16_t x, int16_t y, int16_t width, int16_t height,
                const char* title) {
  epaper.drawRoundRect(x, y, width, height, 10, EPAPER_BLACK);
  epaper.fillRoundRect(x + 1, y + 1, width - 2, 30, 9, EPAPER_BLACK);
  epaper.setTextColor(EPAPER_WHITE);
  printAt(x + 12, y + 8, 2, title);
  epaper.setTextColor(EPAPER_BLACK);
}

void drawDashboard(const logger_core::SiteSnapshot& snapshot,
                   const char* refreshUtc) {
  char line[96];
  char age[24];

  epaper.fillScreen(EPAPER_WHITE);
  epaper.fillRect(0, 0, epaper.width(), 38, EPAPER_BLACK);
  epaper.setTextColor(EPAPER_WHITE);
  snprintf(line, sizeof(line), "%.18s", DEVICE_ID);
  printAt(10, 10, 2, line);
  snprintf(line, sizeof(line), "%s  WiFi %s %lddBm",
           logger_core::siteHealthName(snapshot.health),
           snapshot.wifiConnected ? "ON" : "OFF",
           static_cast<long>(snapshot.wifiRssiDbm));
  printAt(250, 10, 2, line);
  if (snapshot.wifiConnected) {
    snprintf(line, sizeof(line), "%u.%u.%u.%u",
             snapshot.ipAddress[0], snapshot.ipAddress[1],
             snapshot.ipAddress[2], snapshot.ipAddress[3]);
  } else {
    snprintf(line, sizeof(line), "OFFLINE");
  }
  printRightAligned(790, 10, 2, line);
  epaper.setTextColor(EPAPER_BLACK);

  drawWidget(10, 50, 380, 170, "WATER");
  if (snapshot.waterValid) {
    snprintf(line, sizeof(line), "%.3f m", snapshot.waterLevelM);
    printAt(28, 112, 4, line);
    snprintf(line, sizeof(line), "Temperature  %.2f C",
             snapshot.waterTemperatureC);
    printAt(28, 154, 2, line);
  } else {
    printAt(28, 112, 4, "NO READING");
  }
  formatAge(snapshot.probeAgeValid, snapshot.probeAgeSeconds, age, sizeof(age));
  snprintf(line, sizeof(line), "Probe %s  ID %u  SN %lu", age,
           snapshot.sensorModbusId,
           static_cast<unsigned long>(snapshot.sensorSerialNumber));
  printAt(28, 194, 1, line);

  drawWidget(410, 50, 380, 170, "WEATHER");
  if (snapshot.weatherValid) {
    snprintf(line, sizeof(line), "%.1f C   %.0f%% RH",
             snapshot.airTemperatureC, snapshot.relativeHumidityPct);
    printAt(428, 102, 3, line);
    snprintf(line, sizeof(line), "Pressure %.0f hPa",
             snapshot.barometricPressureHpa);
    printAt(428, 140, 2, line);
    snprintf(line, sizeof(line), "Wind %.1f m/s @ %.0f deg",
             snapshot.windSpeedMs, snapshot.windDirectionDeg);
    printAt(428, 170, 2, line);
    if (isfinite(snapshot.rainfallIntervalMm)) {
      snprintf(line, sizeof(line), "Rain interval %.1f mm",
               snapshot.rainfallIntervalMm);
    } else {
      snprintf(line, sizeof(line), "Rain interval --");
    }
    printAt(428, 200, 2, line);
  } else {
    printAt(428, 112, 3,
            snapshot.weatherEnabled ? "NO WEATHER DATA" : "DISABLED");
  }

  drawWidget(10, 235, 380, 170, "POWER");
  if (snapshot.batteryValid) {
    snprintf(line, sizeof(line), "Battery %.2f V   %.0f%%",
             snapshot.batteryVoltageV, snapshot.batteryChargePct);
    printAt(28, 290, 2, line);
  } else {
    printAt(28, 290, 3, "BATTERY --");
  }
  if (snapshot.batteryExtrema24hValid) {
    snprintf(line, sizeof(line), "24h %.2f - %.2f V",
             snapshot.batteryVoltageMin24hV,
             snapshot.batteryVoltageMax24hV);
    printAt(28, 334, 2, line);
  }
  if (snapshot.solarValid) {
    snprintf(line, sizeof(line), "Solar %.2f V  %.2f A  %.1f W",
             snapshot.solarVoltageV, snapshot.solarCurrentA,
             snapshot.solarPowerW);
    printAt(28, 372, 2, line);
    printAt(28, 396, 1,
            snapshot.solarCharging ? "CHARGING" : "IDLE");
  } else {
    printAt(28, 378, 2, "Solar --");
  }

  drawWidget(410, 235, 380, 170, "UPLOAD / LOGGER");
  formatAge(
      snapshot.uploadAgeValid, snapshot.uploadAgeSeconds, age, sizeof(age));
  snprintf(line, sizeof(line), "Last upload %s", age);
  printAt(428, 290, 3, line);
  snprintf(line, sizeof(line), "Backlog %lu / %lu",
           static_cast<unsigned long>(snapshot.backlogCount),
           static_cast<unsigned long>(BACKLOG_CAPACITY));
  printAt(428, 334, 2, line);
  snprintf(line, sizeof(line), "Failures %lu   Clock %s",
           static_cast<unsigned long>(snapshot.consecutiveUploadFailures),
           snapshot.clockValid ? "SYNCED" : "INVALID");
  printAt(428, 372, 2, line);

  epaper.drawFastHLine(10, 416, 780, EPAPER_BLACK);
  snprintf(
      line, sizeof(line), "UI refreshed: %s",
      refreshUtc != nullptr && refreshUtc[0] != '\0'
          ? refreshUtc
          : "clock unavailable");
  printAt(12, 426, 2, line);
  snprintf(
      line, sizeof(line),
      "5m weather | quiet %02u:%02u-%02u:%02u | /status | %s",
      static_cast<unsigned>(GIGA_EPAPER_QUIET_START_MINUTE / 60U),
      static_cast<unsigned>(GIGA_EPAPER_QUIET_START_MINUTE % 60U),
      static_cast<unsigned>(GIGA_EPAPER_ACTIVE_START_MINUTE / 60U),
      static_cast<unsigned>(GIGA_EPAPER_ACTIVE_START_MINUTE % 60U),
      uploadEndpointModeName());
  printAt(12, 454, 1, line);
}

bool waitForDisplayIdle(uint32_t timeoutMs) {
  const uint32_t startedMs = millis();
  while (digitalRead(GIGA_EPAPER_BUSY_PIN) == GIGA_EPAPER_BUSY_LEVEL) {
    kickSystemWatchdog();
    if (logger_core::epaperBusyTimedOut(
            millis(), startedMs, timeoutMs)) {
      return false;
    }
    delay(1);
  }
  return true;
}

bool clearDisplayWhite(const char* successMessage) {
  if (!waitForDisplayIdle(GIGA_EPAPER_BUSY_TIMEOUT_MS)) {
    Serial.println("E-paper BUSY timeout before boot clear");
    return false;
  }

  displayAwake = true;
  epaper.setFullWindow();
  epaper.firstPage();
  do {
    epaper.fillScreen(EPAPER_WHITE);
  } while (epaper.nextPage());
  // The driver already waits for BUSY during nextPage(). Do not impose a
  // second full timeout when that bounded wait reports a stuck panel.
  if (digitalRead(GIGA_EPAPER_BUSY_PIN) == GIGA_EPAPER_BUSY_LEVEL) {
    Serial.println("E-paper BUSY timeout after boot clear");
    displayAwake = false;
    return false;
  }
  epaper.powerOff();
  displayAwake = false;
  displayIsWhite = true;
  Serial.println(successMessage);
  return true;
}

bool clearDisplayForNight() {
  powerCycleDisplayController();
  initializeDisplayDriver(false);
  return clearDisplayWhite("E-paper cleared for nightly quiet window");
}

bool displayActiveNow() {
  if (!isClockValid()) return true;

  static bool timezoneConfigured = false;
  if (!timezoneConfigured) {
    setenv("TZ", GIGA_EPAPER_TIMEZONE, 1);
    tzset();
    timezoneConfigured = true;
  }

  const time_t now = time(nullptr);
  struct tm localTime;
  if (localtime_r(&now, &localTime) == nullptr) return true;
  const uint16_t minuteOfDay =
      static_cast<uint16_t>(localTime.tm_hour * 60 + localTime.tm_min);
  return logger_core::minuteInDailyWindow(
      minuteOfDay, GIGA_EPAPER_ACTIVE_START_MINUTE,
      GIGA_EPAPER_QUIET_START_MINUTE);
}

bool renderSnapshot(
    const logger_core::SiteSnapshot& snapshot,
    logger_core::EpaperRefreshDecision decision,
    const char* refreshUtc) {
  if (decision == logger_core::EpaperRefreshDecision::FULL) {
    // Long sensor and network operations can leave the powered controller in
    // stale state. Reinitialize it before every infrequent full refresh.
    powerCycleDisplayController();
    initializeDisplayDriver(false);
  }
  if (!waitForDisplayIdle(GIGA_EPAPER_BUSY_TIMEOUT_MS)) {
    Serial.println("E-paper BUSY timeout before refresh");
    return false;
  }

  if (decision == logger_core::EpaperRefreshDecision::FULL) {
    epaper.setFullWindow();
  } else {
    epaper.setPartialWindow(0, 0, epaper.width(), epaper.height());
  }

  displayAwake = true;
  const uint32_t refreshStartedMs = millis();
  epaper.firstPage();
  do {
    drawDashboard(snapshot, refreshUtc);
  } while (epaper.nextPage());
  const bool refreshTimedOut =
      digitalRead(GIGA_EPAPER_BUSY_PIN) == GIGA_EPAPER_BUSY_LEVEL &&
      millis() - refreshStartedMs >= GIGA_EPAPER_BUSY_TIMEOUT_MS;
  if (refreshTimedOut) {
    Serial.println("E-paper BUSY timeout after refresh");
    displayAwake = false;
    return false;
  }

  // Partial paging writes the previous-image plane after the visible update.
  // The controller can briefly reassert BUSY there; power-off completes that
  // sequence, as exercised by the HIL refresh path.
  epaper.powerOff();
  if (digitalRead(GIGA_EPAPER_BUSY_PIN) == GIGA_EPAPER_BUSY_LEVEL) {
    Serial.println("E-paper BUSY timeout during power-off");
    displayAwake = false;
    return false;
  }
  displayAwake = false;
  displayIsWhite = false;
  return true;
}

}  // namespace

bool initDisplay() {
  if (!GIGA_EPAPER_ENABLED) {
    displayPresent = false;
    return false;
  }

  powerCycleDisplayController();
  initializeDisplayDriver(true);
  if (!clearDisplayWhite("E-paper cleared for boot")) {
    displayPresent = false;
    return false;
  }
  displayPresent = true;
  displayRefreshPending = true;
  refreshState = logger_core::EpaperRefreshState();
  Serial.println(
      "Waveshare 4.26-inch GDEQ0426T82 e-paper initialized on SPI1");
  return true;
}

void wakeDisplayForTimeout() {
  lastDisplayWakeRequestMs = millis();
  lastDisplayWakeRequestUtcValue = nowUtcString();
  displayRefreshPending = true;
}

void updateDisplay() {
  const uint32_t nowMs = millis();
  if (!displayPresent) {
    if (logger_core::deadlinePending(nowMs, nextDisplayAttemptMs)) return;
    if (!initDisplay()) {
      nextDisplayAttemptMs = nowMs + DISPLAY_RETRY_INTERVAL_MS;
      return;
    }
    ++displayRecoveryCountLocal;
  }

  const bool activeNow = displayActiveNow();
  if (!activeNow) {
    displayRefreshPending = false;
    if (!displayQuietWindowActive) {
      if (!displayIsWhite && !clearDisplayForNight()) {
        displayPresent = false;
        nextDisplayAttemptMs = nowMs + DISPLAY_RETRY_INTERVAL_MS;
        return;
      }
      displayQuietWindowActive = true;
      lastDisplayRefreshMs = nowMs;
      lastDisplayRefreshUtcValue = nowUtcString();
      ++displayRefreshCountLocal;
    }
    return;
  }

  logger_core::SiteSnapshot snapshot = currentSiteSnapshot();
  logger_core::EpaperRefreshDecision decision =
      logger_core::decideEpaperRefresh(
          nowMs, snapshot, GIGA_EPAPER_REFRESH_INTERVAL_MS,
          GIGA_EPAPER_FULL_REFRESH_INTERVAL_MS, refreshState);
  if (displayQuietWindowActive) {
    displayQuietWindowActive = false;
    decision = logger_core::EpaperRefreshDecision::FULL;
  }
  if (displayRefreshPending &&
      decision == logger_core::EpaperRefreshDecision::NONE) {
    decision = logger_core::EpaperRefreshDecision::PARTIAL;
  }
  if (decision == logger_core::EpaperRefreshDecision::NONE) return;

  displayRefreshPending = false;
  const String refreshUtc = nowUtcString();
  if (!renderSnapshot(snapshot, decision, refreshUtc.c_str())) {
    displayPresent = false;
    nextDisplayAttemptMs = nowMs + DISPLAY_RETRY_INTERVAL_MS;
    return;
  }

  logger_core::recordEpaperRefresh(
      nowMs, snapshot, decision, refreshState);
  lastDisplayRefreshMs = nowMs;
  lastDisplayRefreshUtcValue = refreshUtc;
  ++displayRefreshCountLocal;
  Serial.print("E-paper dashboard refreshed: ");
  Serial.print(
      decision == logger_core::EpaperRefreshDecision::FULL
          ? "full"
          : "partial");
  Serial.print(" UTC=");
  Serial.println(
      refreshUtc.length() > 0 ? refreshUtc : String("clock unavailable"));
}

void sleepDisplay() {
  epaper.powerOff();
  displayAwake = false;
}

String lastDisplayWakeRequestUtc() {
  return lastDisplayWakeRequestUtcValue;
}

String lastDisplayRefreshUtc() {
  return lastDisplayRefreshUtcValue;
}

String lastDisplayWakeRequestAge() {
  return lastDisplayWakeRequestMs == 0
             ? String("never")
             : millisAgeString(lastDisplayWakeRequestMs);
}

String lastDisplayRefreshAge() {
  return lastDisplayRefreshMs == 0
             ? String("never")
             : millisAgeString(lastDisplayRefreshMs);
}

uint32_t displayRefreshCount() {
  return displayRefreshCountLocal;
}

uint32_t displayI2cRecoveryCount() {
  return displayRecoveryCountLocal;
}
#endif
