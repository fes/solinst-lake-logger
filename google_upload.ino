String makePayload(const ProbeReading &r) {
  float batteryChargePct = approximateBatteryChargePercent(r);
  bool solarChargingNow = solarChargingBatteryNow(r);

  String payload = "{";
  payload += "\"secret\":\"" + jsonEscape(String(SHARED_SECRET)) + "\",";
  payload += "\"device_id\":\"" + jsonEscape(String(DEVICE_ID)) + "\",";
  payload += "\"timestamp_utc\":\"" + jsonEscape(r.timestampUtc) + "\",";
  payload += "\"modbus_id\":" + String(detectedSensorId) + ",";
  payload += "\"serial_number\":\"" + String(detectedIdentity.serialNumber) + "\",";
  payload += "\"firmware\":\"" + String(detectedIdentity.fwMajor) + "." + String(detectedIdentity.fwMinor) + "\",";
  payload += "\"water_level_m\":" + String(r.level, 4) + ",";
  payload += "\"temperature_c\":" + String(r.temperature, 3) + ",";

  appendPowerMonitorJson(payload, "battery_output", r.batteryOutput);
  appendPowerMonitorJson(payload, "solar_input", r.solarInput);

  payload += "\"battery_charge_level_pct_approx\":";
  payload += (isfinite(batteryChargePct) ? String(batteryChargePct, 1) : String("null"));
  payload += ",";
  payload += "\"solar_charging_battery\":" + String(solarChargingNow ? "true" : "false") + ",";

  payload += "\"status\":\"OK\"";
  payload += "}";
  return payload;
}

void recordUploadFailure(const String &reason) {
  consecutiveUploadFailures++;
  lastUploadError = reason;
  lastUploadErrorUtc = nowUtcString();

  unsigned long cooldown = UPLOAD_RETRY_COOLDOWN_INITIAL_MS;
  for (uint32_t i = 1; i < consecutiveUploadFailures; i++) {
    if (cooldown >= UPLOAD_RETRY_COOLDOWN_MAX_MS / 2UL) {
      cooldown = UPLOAD_RETRY_COOLDOWN_MAX_MS;
      break;
    }
    cooldown *= 2UL;
  }
  cooldown = min(cooldown, UPLOAD_RETRY_COOLDOWN_MAX_MS);
  nextUploadAllowedMs = millis() + cooldown;

  Serial.print("Upload cooldown set to ms: ");
  Serial.println(cooldown);
  Serial.print("Upload failure reason: ");
  Serial.println(reason);
}

void clearUploadFailureState() {
  consecutiveUploadFailures = 0;
  nextUploadAllowedMs = 0;
  lastUploadError = "";
  lastUploadErrorUtc = "";
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

  if (statusCode == 200) {
    return true;
  }

  recordUploadFailure(String("HTTP ") + statusCode + String(": ") + response);
  return false;
}

bool postReadingWithRetry(const ProbeReading &r) {
  unsigned long nowMs = millis();
  if (nowMs < nextUploadAllowedMs) {
    Serial.print("Upload skipped due to cooldown; ms remaining: ");
    Serial.println(nextUploadAllowedMs - nowMs);
    return false;
  }

  if (!connectWiFi()) {
    recordUploadFailure("Wi-Fi not connected");
    return false;
  }

  lastUploadAttemptMs = millis();

  for (int attempt = 1; attempt <= POST_RETRIES; attempt++) {
    if (postJson(makePayload(r))) {
      lastSuccessfulUploadUtc = nowUtcString();
      lastSuccessfulUploadMs = millis();
      successfulUploads++;
      clearUploadFailureState();
      return true;
    }

    if (attempt < POST_RETRIES) {
      delay(POST_RETRY_DELAY_MS);
    }
  }

  failedUploads++;
  if (lastUploadError.length() == 0) {
    recordUploadFailure("Upload failed after retries");
  }
  return false;
}

void flushBacklogOnce() {
  if (backlogCount == 0) return;
  if (millis() < nextUploadAllowedMs) return;

  ProbeReading r;
  if (!peekBacklog(r)) return;

  if (postReadingWithRetry(r)) {
    ProbeReading dropped;
    dequeueReading(dropped);
    Serial.println("Uploaded one backlog reading");
  } else {
    Serial.println("Backlog upload attempt failed or deferred");
  }
}
