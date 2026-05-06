String makePayload(const ProbeReading &r) {
  String payload = "{";
  payload += "\"secret\":\"" + jsonEscape(String(SHARED_SECRET)) + "\",";
  payload += "\"device_id\":\"" + jsonEscape(String(DEVICE_ID)) + "\",";
  payload += "\"timestamp_utc\":\"" + jsonEscape(r.timestampUtc) + "\",";
  payload += "\"modbus_id\":" + String(detectedSensorId) + ",";
  payload += "\"serial_number\":\"" + String(detectedIdentity.serialNumber) + "\",";
  payload += "\"firmware\":\"" + String(detectedIdentity.fwMajor) + "." + String(detectedIdentity.fwMinor) + "\",";
  payload += "\"water_level_m\":" + String(r.level, 4) + ",";
  payload += "\"temperature_c\":" + String(r.temperature, 3) + ",";
  payload += "\"status\":\"OK\"";
  payload += "}";
  return payload;
}

bool postJson(const String &payload) {
  httpClient.stop();

  httpClient.beginRequest();
  httpClient.post(POST_PATH);
  httpClient.sendHeader("Content-Type", "application/json");
  httpClient.sendHeader("Content-Length", payload.length());
  httpClient.beginBody();
  httpClient.print(payload);
  httpClient.endRequest();

  int statusCode = httpClient.responseStatusCode();
  String response = httpClient.responseBody();

  Serial.print("POST status: ");
  Serial.println(statusCode);
  Serial.print("POST response: ");
  Serial.println(response);

  return statusCode == 200;
}

bool postReadingWithRetry(const ProbeReading &r) {
  if (!connectWiFi()) {
    return false;
  }

  for (int attempt = 1; attempt <= POST_RETRIES; attempt++) {
    if (postJson(makePayload(r))) {
      lastSuccessfulUploadUtc = nowUtcString();
      lastSuccessfulUploadMs = millis();
      successfulUploads++;
      return true;
    }

    if (attempt < POST_RETRIES) {
      delay(POST_RETRY_DELAY_MS);
    }
  }

  failedUploads++;
  return false;
}

void flushBacklogOnce() {
  if (backlogCount == 0) return;

  ProbeReading r;
  if (!peekBacklog(r)) return;

  if (postReadingWithRetry(r)) {
    ProbeReading dropped;
    dequeueReading(dropped);
    Serial.println("Uploaded one backlog reading");
  } else {
    Serial.println("Backlog upload attempt failed");
  }
}