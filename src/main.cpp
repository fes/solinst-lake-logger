#include <Arduino.h>

#include "config.h"
#include "runtime_boundaries.h"

namespace {

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

void setup() {
  Serial.begin(115200);
#if defined(LOGGER_WAIT_FOR_SERIAL_ON_BOOT)
  const unsigned long serialWaitStartedMs = millis();
  while (!Serial && millis() - serialWaitStartedMs < 15000UL) delay(10);
#else
  delay(2000);
#endif
  bootMs = millis();

  Serial.println();
  Serial.print("Starting ");
  Serial.print(ACTIVE_BOARD_PROFILE.name);
  Serial.println(" logger + HTTP API");

  ACTIVE_PLATFORM.begin();

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
}

void loop() {
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
