#include "config.h"

void setup() {
  Serial.begin(115200);
  delay(2000);
  bootMs = millis();

  Serial.println();
  Serial.println("Starting Opta + Solinst 301 + Google Sheets logger + HTTP API");

  loadRuntimeConfig();
  printRuntimeConfigSummary();

  if (!ModbusRTUClient.begin(WLTS_BAUD, WLTS_SERIAL_CFG)) {
    Serial.println("ERROR: Failed to start Modbus RTU client");
    while (1) delay(1000);
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
    delay(10);
    return;
  }

  if (detectedSensorId != 0 && shouldLogNow()) {
    ProbeReading r;
    if (probeNow(r)) {
      Serial.print("Water level: ");
      Serial.print(r.level, 4);
      Serial.print(" ");
      Serial.println(LEVEL_UNITS);

      Serial.print("Temperature: ");
      Serial.print(r.temperature, 3);
      Serial.print(" ");
      Serial.println(TEMP_UNITS);

      if (!postReadingWithRetry(r)) {
        if (!enqueueReading(r)) {
          droppedBacklogEntries++;
          Serial.println("Backlog full; dropping reading");
        } else {
          Serial.println("Queued reading in backlog");
        }
      } else {
        Serial.println("Posted reading to Google Sheets");
      }
    } else {
      Serial.println("Measurement read failed after retries");
    }

    Serial.println("---");
  }

  flushBacklogOnce();

  delay(10);
}
