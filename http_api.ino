void sendHttpJson(WiFiClient &client, int statusCode, const String &body) {
  client.print("HTTP/1.1 ");
  client.print(statusCode);
  if (statusCode == 200) client.println(" OK");
  else if (statusCode == 404) client.println(" Not Found");
  else if (statusCode == 500) client.println(" Internal Server Error");
  else client.println();

  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.print(body);
}

namespace {

float approximateBatteryChargePercent(const ProbeReading &reading) {
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

bool solarChargingBatteryNow(const ProbeReading &reading) {
  return reading.solarInput.valid && isfinite(reading.solarInput.currentA) && reading.solarInput.currentA > 0.05f;
}

void appendPowerMonitorJson(String &body, const char *prefix, const PowerMonitorSnapshot &snapshot) {
  body += "\"" + String(prefix) + "_monitor_present\":" + String(snapshot.present ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_monitor_valid\":" + String(snapshot.valid ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_voltage_v\":";
  body += (snapshot.valid ? String(snapshot.busVoltageV, 3) : String("null"));
  body += ",";
  body += "\"" + String(prefix) + "_current_a\":";
  body += (snapshot.valid ? String(snapshot.currentA, 4) : String("null"));
  body += ",";
  body += "\"" + String(prefix) + "_power_w\":";
  body += (snapshot.valid ? String(snapshot.powerW, 4) : String("null"));
  body += ",";
}

} // namespace

String probeJson(const ProbeReading &r) {
  String body = "{";
  body += "\"ok\":" + String(r.valid ? "true" : "false") + ",";
  body += "\"timestamp_utc\":\"" + jsonEscape(r.timestampUtc) + "\",";
  body += "\"water_level_m\":";
  body += (r.valid ? String(r.level, 4) : String("null"));
  body += ",";
  body += "\"temperature_c\":";
  body += (r.valid ? String(r.temperature, 3) : String("null"));
  body += ",";
  body += "\"units\":{\"level\":\"m\",\"temperature\":\"C\"}";
  body += "}";
  return body;
}

String statusJson() {
  ProbeReading powerSnapshot = lastProbeReading;
  readPowerMonitors(powerSnapshot);
  float batteryChargePct = approximateBatteryChargePercent(powerSnapshot);
  bool solarChargingNow = solarChargingBatteryNow(powerSnapshot);

  String body = "{";
  body += "\"device_id\":\"" + jsonEscape(String(DEVICE_ID)) + "\",";
  body += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  body += "\"wifi_rssi_dbm\":" + String((WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0) + ",";
  body += "\"ip\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\",";
  body += "\"uptime_s\":" + String((millis() - bootMs) / 1000UL) + ",";
  body += "\"clock_valid\":" + String(clockValid ? "true" : "false") + ",";
  body += "\"clock_valid_check\":" + String(isClockValid() ? "true" : "false") + ",";
  body += "\"clock_now_utc\":\"" + jsonEscape(nowUtcString()) + "\",";
  body += "\"last_ntp_sync_age\":\"" + jsonEscape(millisAgeString(lastNtpSyncMs)) + "\",";
  body += "\"sensor_found\":" + String(detectedSensorId != 0 ? "true" : "false") + ",";
  body += "\"modbus_id\":" + String(detectedSensorId) + ",";
  body += "\"serial_number\":\"" + String(detectedIdentity.serialNumber) + "\",";
  body += "\"firmware\":\"" + String(detectedIdentity.fwMajor) + "." + String(detectedIdentity.fwMinor) + "\",";
  body += "\"last_successful_probe_read_utc\":\"" + jsonEscape(lastSuccessfulProbeReadUtc) + "\",";
  body += "\"last_successful_probe_read_age\":\"" + jsonEscape(millisAgeString(lastSuccessfulProbeReadMs)) + "\",";
  body += "\"last_successful_upload_utc\":\"" + jsonEscape(lastSuccessfulUploadUtc) + "\",";
  body += "\"last_successful_upload_age\":\"" + jsonEscape(millisAgeString(lastSuccessfulUploadMs)) + "\",";
  body += "\"successful_probe_reads\":" + String(successfulProbeReads) + ",";
  body += "\"failed_probe_reads\":" + String(failedProbeReads) + ",";
  body += "\"successful_uploads\":" + String(successfulUploads) + ",";
  body += "\"failed_uploads\":" + String(failedUploads) + ",";
  body += "\"backlog_count\":" + String(backlogCount) + ",";
  body += "\"dropped_backlog_entries\":" + String(droppedBacklogEntries) + ",";
  body += "\"power_monitor_init_status\":\"" + jsonEscape(powerMonitorInitStatus) + "\",";

  appendPowerMonitorJson(body, "battery_output", powerSnapshot.batteryOutput);
  appendPowerMonitorJson(body, "solar_input", powerSnapshot.solarInput);

  body += "\"battery_charge_level_pct_approx\":";
  body += (isfinite(batteryChargePct) ? String(batteryChargePct, 1) : String("null"));
  body += ",";
  body += "\"solar_charging_battery\":" + String(solarChargingNow ? "true" : "false");
  body += "}";
  return body;
}

void handleHttpClient() {
  WiFiClient client = server.available();
  if (!client) return;

  String requestLine = "";
  unsigned long start = millis();

  while (client.connected() && millis() - start < 2000UL) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n') break;
      if (c != '\r') requestLine += c;
    }
  }

  while (client.connected() && millis() - start < 4000UL) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line == "\r" || line.length() == 0) break;
    } else {
      delay(1);
    }
  }

  Serial.print("HTTP request: ");
  Serial.println(requestLine);

  if (requestLine.startsWith("GET /probe")) {
    ProbeReading r;
    if (probeNow(r)) {
      sendHttpJson(client, 200, probeJson(r));
    } else {
      sendHttpJson(client, 500, "{\"ok\":false,\"error\":\"probe read failed\"}");
    }
  } else if (requestLine.startsWith("GET /status")) {
    sendHttpJson(client, 200, statusJson());
  } else if (requestLine.startsWith("GET /reset")) {
    sendHttpJson(client, 200, "{\"ok\":true,\"message\":\"rebooting\"}");
    client.flush();
    delay(200);
    NVIC_SystemReset();
  } else {
    sendHttpJson(client, 404,
      "{\"ok\":false,\"error\":\"not found\",\"routes\":[\"/probe\",\"/status\",\"/reset\"]}");
  }

  delay(1);
  client.stop();
}
