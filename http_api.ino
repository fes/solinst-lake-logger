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
  body += "\"dropped_backlog_entries\":" + String(droppedBacklogEntries);
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