#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  bootMs = millis();

  Serial.println();
  Serial.println("Starting Opta + Solinst 301 + Google Sheets logger + HTTP API");

  initUserInterface();

  loadRuntimeConfig();
  printRuntimeConfigSummary();
  initPowerMonitors();

  if (!ModbusRTUClient.begin(WLTS_BAUD, WLTS_SERIAL_CFG)) {
    Serial.println("ERROR: Failed to start Modbus RTU client");
    while (1) {
      updateUserInterface();
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

  if (manualProbeRequested) {
    manualProbeRequested = false;
    if (!isClockValid()) {
      syncClockFromNtp();
    }
    performProbeAndUpload("manual button press");
  }

  if (!isClockValid()) {
    static unsigned long lastClockWarningMs = 0;
    if (millis() - lastClockWarningMs > 10000UL) {
      Serial.println("Clock is not valid yet; skipping scheduled logging");
      lastClockWarningMs = millis();
    }

    flushBacklogOnce();
    updateUserInterface();
    delay(10);
    return;
  }

  if (detectedSensorId != 0 && shouldLogNow()) {
    performProbeAndUpload("scheduled interval");
  }

  flushBacklogOnce();
  updateUserInterface();

  delay(10);
}
