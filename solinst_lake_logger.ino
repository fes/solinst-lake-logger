#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  bootMs = millis();

  Serial.println();
  Serial.println("Starting Opta + Solinst 301 lake logger + HTTP API");

  initUserInterface();

  loadRuntimeConfig();
  printRuntimeConfigSummary();
  initPowerMonitors();
  initWeatherSensor();
  initDisplay();

  if (!ModbusRTUClient.begin(WLTS_BAUD, WLTS_SERIAL_CFG)) {
    Serial.println("ERROR: Failed to start Modbus RTU client");
    while (1) {
      updateUserInterface();
      updateDisplay();
      delay(25);
    }
  }

  if (scanForSensor(SCAN_START_ID, SCAN_END_ID, detectedSensorId, detectedIdentity)) {
    printIdentity(detectedSensorId, detectedIdentity);
  } else {
    Serial.println("No Solinst 301 found in scan range");
  }

  connectWiFi();
  syncClockFromNtp();

  if (detectedSensorId != 0 && isClockValid()) {
    ProbeReading startupReading;
    if (probeNow(startupReading)) {
      Serial.println("Startup probe captured initial cached reading");
    } else {
      Serial.println("Startup probe failed; display will wait for later data");
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    server.begin();
    Serial.print("HTTP server started at http://");
    Serial.print(WiFi.localIP());
    Serial.print(":");
    Serial.println(HTTP_PORT);
  }
}

void loop() {
  handleUserButton();
  handleHttpClient();

  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  maintainClockSync();

  if (!isClockValid()) {
    static unsigned long lastClockWarningMs = 0;
    if (millis() - lastClockWarningMs > 10000UL) {
      Serial.println("Clock is not valid yet; skipping scheduled logging");
      lastClockWarningMs = millis();
    }

    flushBacklogOnce();
    updateUserInterface();
    updateDisplay();
    delay(10);
    return;
  }

  pollWeatherIfDue(false);

  if (detectedSensorId != 0 && shouldLogNow()) {
    performProbeAndUpload("scheduled interval");
  }

  flushBacklogOnce();
  updateUserInterface();
  updateDisplay();

  delay(10);
}
