#include <Arduino.h>
#include <mbed.h>

#include "config.h"
#include "runtime_boundaries.h"

namespace {

mbed::Watchdog& systemWatchdog = mbed::Watchdog::get_instance();
reset_reason_t systemResetReason = RESET_REASON_UNKNOWN;

const char* resetReasonName(reset_reason_t reason) {
  switch (reason) {
    case RESET_REASON_POWER_ON: return "power_on";
    case RESET_REASON_PIN_RESET: return "pin_reset";
    case RESET_REASON_BROWN_OUT: return "brown_out";
    case RESET_REASON_SOFTWARE: return "software";
    case RESET_REASON_WATCHDOG: return "watchdog";
    case RESET_REASON_LOCKUP: return "lockup";
    case RESET_REASON_WAKE_LOW_POWER: return "wake_low_power";
    case RESET_REASON_ACCESS_ERROR: return "access_error";
    case RESET_REASON_BOOT_ERROR: return "boot_error";
    case RESET_REASON_MULTIPLE: return "multiple";
    case RESET_REASON_PLATFORM: return "platform";
    case RESET_REASON_UNKNOWN: return "unknown";
  }
  return "unknown";
}

void configureNetworkTimeouts() {
  wifiClient.setSocketTimeout(NETWORK_SOCKET_TIMEOUT_MS);
  wifiSslClient.setSocketTimeout(NETWORK_SOCKET_TIMEOUT_MS);
  googleAppsScriptHttpClient.setHttpResponseTimeout(
      HTTP_RESPONSE_TIMEOUT_MS);
  fesLabsHttpsClient.setHttpResponseTimeout(HTTP_RESPONSE_TIMEOUT_MS);
  fesLabsHttpClient.setHttpResponseTimeout(HTTP_RESPONSE_TIMEOUT_MS);
}

void startSystemWatchdog() {
  const uint32_t timeoutMs =
      min(SYSTEM_WATCHDOG_TIMEOUT_MS, systemWatchdog.get_max_timeout());
  if (!systemWatchdog.is_running()) {
    systemWatchdog.start(timeoutMs);
  }
  Serial.print("System watchdog timeout ms: ");
  Serial.println(systemWatchdog.get_timeout());
}

void ensureHttpServerStarted() {
  if (!logger_core::shouldStartHttpServer(
          WiFi.status() == WL_CONNECTED, httpServerStarted)) {
    return;
  }

  server.begin();
  // Arduino Mbed keeps the listener attached to the Wi-Fi network interface
  // across link loss, so "started" is a process-lifetime state.
  httpServerStarted = true;
  Serial.print("HTTP server started at http://");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.println(HTTP_PORT);
}

}  // namespace

void kickSystemWatchdog() {
  if (systemWatchdog.is_running()) systemWatchdog.kick();
}

const char* lastSystemResetReasonName() {
  return resetReasonName(systemResetReason);
}

void setup() {
  Serial.begin(115200);
#if defined(LOGGER_WAIT_FOR_SERIAL_ON_BOOT)
  const unsigned long serialWaitStartedMs = millis();
  while (!Serial && millis() - serialWaitStartedMs < 15000UL) delay(10);
#else
  delay(2000);
#endif
  bootMs = millis();
  systemResetReason = mbed::ResetReason::get();

  Serial.println();
  Serial.print("Starting ");
  Serial.print(ACTIVE_BOARD_PROFILE.name);
  Serial.println(" logger + HTTP API");
  Serial.print("Last system reset reason: ");
  Serial.println(lastSystemResetReasonName());

  ACTIVE_PLATFORM.begin();
  configureNetworkTimeouts();

  loadRuntimeConfig();
  printRuntimeConfigSummary();
  ACTIVE_AUXILIARY_SENSORS.begin();
  ACTIVE_DISPLAY.begin();

  attemptSensorDiscovery();

  connectWiFi();
  syncClockFromNtp();

  if (detectedSensorId != 0 && isClockValid()) {
#if defined(LOGGER_COMMISSIONING_UPLOAD_ON_BOOT)
    performProbeAndUpload("commissioning upload test");
#else
    ProbeReading startupReading;
    if (probeNow(startupReading)) {
      Serial.println("Startup probe captured initial cached reading");
    } else {
      Serial.println("Startup probe failed; display will wait for later data");
    }
#endif
  }

  ensureHttpServerStarted();
  startSystemWatchdog();
}

void loop() {
  kickSystemWatchdog();
  ACTIVE_PLATFORM.handleInput();
  ensureHttpServerStarted();
  handleHttpClient();
  maintainSensorDiscovery();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    ensureHttpServerStarted();
  }
  maintainClockSync();
  ACTIVE_AUXILIARY_SENSORS.pollPowerIfDue(false);

  if (!isClockValid()) {
    static unsigned long lastClockWarningMs = 0;
    if (millis() - lastClockWarningMs > 10000UL) {
      Serial.println("Clock is not valid yet; skipping scheduled logging");
      lastClockWarningMs = millis();
    }

    flushBacklogOnce();
    ACTIVE_PLATFORM.tick();
    ACTIVE_DISPLAY.tick();
    delay(10);
    return;
  }

  ACTIVE_AUXILIARY_SENSORS.pollIfDue(false);

  if (detectedSensorId != 0 && shouldLogNow()) {
    performProbeAndUpload("scheduled interval");
  }

  flushBacklogOnce();
  ACTIVE_PLATFORM.tick();
  ACTIVE_DISPLAY.tick();

  delay(10);
}
