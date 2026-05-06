namespace {

float approximateBatteryChargePercentForUpload(const ProbeReading &reading) {
  if (!reading.batteryOutput.valid || !isfinite(reading.batteryOutput.busVoltageV)) {
    return NAN;
  }

  // Very rough LiFePO4 estimate from battery voltage only.
  // This is most meaningful near-rest and less accurate while charging/discharging.
  const float emptyV = 12.0f;
  const float fullV = 13.4f;
  float pct = ((reading.batteryOutput.busVoltageV - emptyV) / (fullV - emptyV)) * 100.0f;

  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

bool solarChargingBatteryNowForUpload(const ProbeReading &reading) {
  return reading.solarInput.valid && isfinite(reading.solarInput.currentA) && reading.solarInput.currentA > 0.05f;
}

} // namespace

String makePayload(const ProbeReading &r) {
  float batteryChargePct = approximateBatteryChargePercentForUpload(r);
  bool solarChargingNow = solarChargingBatteryNowForUpload(r);

  String payload = "{";
  payload += "\"secret\":\"" + jsonEscape(String(SHARED_SECRET)) + "\",";
  payload += "\"device_id\":\"" + jsonEscape(String(DEVICE_ID)) + "\",";
  payload += "\"timestamp_utc\":\"" + jsonEscape(r.timestampUtc) + "\",";
  payload += "\"modbus_id\":" + String(detectedSensorId) + ",";
  payload += "\"serial_number\":\"" + String(detectedIdentity.serialNumber) + "\",";
  payload += "\"firmware\":\"" + String(detectedIdentity.fwMajor) + "." + String(detectedIdentity.fwMinor) + "\",";
  payload += "\"water_level_m\":" + String(r.level, 4) + ",";
  payload += "\"temperature_c\":" + String(r.temperature, 3) + ",";

  payload += "\"battery_output_monitor_present\":" + String(r.batteryOutput.present ? "true" : "false") + ",";
  payload += "\"battery_output_monitor_valid\":" + String(r.batteryOutput.valid ? "true" : "false") + ",";
  payload += "\"battery_output_voltage_v\":";
  payload += (r.batteryOutput.valid ? String(r.batteryOutput.busVoltageV, 3) : String("null"));
  payload += ",";
  payload += "\"battery_output_current_a\":";
  payload += (r.batteryOutput.valid ? String(r.batteryOutput.currentA, 4) : String("null"));
  payload += ",";
  payload += "\"battery_output_power_w\":";
  payload += (r.batteryOutput.valid ? String(r.batteryOutput.powerW, 4) : String("null"));
  payload += ",";

  payload += "\"solar_input_monitor_present\":" + String(r.solarInput.present ? "true" : "false") + ",";
  payload += "\"solar_input_monitor_valid\":" + String(r.solarInput.valid ? "true" : "false") + ",";
  payload += "\"solar_input_voltage_v\":";
  payload += (r.solarInput.valid ? String(r.solarInput.busVoltageV, 3) : String("null"));
  payload += ",";
  payload += "\"solar_input_current_a\":";
  payload += (r.solarInput.valid ? String(r.solarInput.currentA, 4) : String("null"));
  payload += ",";
  payload += "\"solar_input_power_w\":";
  payload += (r.solarInput.valid ? String(r.solarInput.powerW, 4) : String("null"));
  payload += ",";

  payload += "\"battery_charge_level_pct_approx\":";
  payload += (isfinite(batteryChargePct) ? String(batteryChargePct, 1) : String("null"));
  payload += ",";
  payload += "\"solar_charging_battery\":" + String(solarChargingNow ? "true" : "false") + ",";

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
