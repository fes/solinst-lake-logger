bool performProbeAndUpload(const char *reason) {
  ProbeReading r;
  if (probeNow(r)) {
    Serial.print("Probe reason: ");
    Serial.println(reason);

    Serial.print("Water level: ");
    Serial.print(r.level, 4);
    Serial.print(" ");
    Serial.println(LEVEL_UNITS);

    Serial.print("Temperature: ");
    Serial.print(r.temperature, 3);
    Serial.print(" ");
    Serial.println(TEMP_UNITS);

    if (r.batteryOutput.valid) {
      Serial.print("Battery output current: ");
      Serial.print(r.batteryOutput.currentA, 3);
      Serial.println(" A");
    }
    if (r.solarInput.valid) {
      Serial.print("Solar input current: ");
      Serial.print(r.solarInput.currentA, 3);
      Serial.println(" A");
    }

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
    resetWeatherSummaryForNextInterval();

    Serial.println("---");
    return true;
  }

  Serial.println("Measurement read failed after retries");
  Serial.println("---");
  return false;
}
