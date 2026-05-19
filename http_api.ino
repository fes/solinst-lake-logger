String prettyJson(const String &compact) {
  String out;
  out.reserve(compact.length() + 64);

  int indent = 0;
  bool inString = false;
  bool escaping = false;

  for (size_t i = 0; i < compact.length(); i++) {
    char c = compact[i];

    if (inString) {
      out += c;
      if (escaping) {
        escaping = false;
      } else if (c == '\\') {
        escaping = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }

    switch (c) {
      case '"':
        inString = true;
        out += c;
        break;

      case '{':
      case '[':
        out += c;
        out += '\n';
        indent++;
        for (int j = 0; j < indent; j++) out += "  ";
        break;

      case '}':
      case ']':
        out += '\n';
        indent = max(0, indent - 1);
        for (int j = 0; j < indent; j++) out += "  ";
        out += c;
        break;

      case ',':
        out += c;
        out += '\n';
        for (int j = 0; j < indent; j++) out += "  ";
        break;

      case ':':
        out += ": ";
        break;

      default:
        if (c != '\n' && c != '\r' && c != '\t') {
          out += c;
        }
        break;
    }
  }

  return out;
}

void sendHttpJson(WiFiClient &client, int statusCode, const String &body) {
  String formattedBody = prettyJson(body);

  client.print("HTTP/1.1 ");
  client.print(statusCode);
  if (statusCode == 200) client.println(" OK");
  else if (statusCode == 404) client.println(" Not Found");
  else if (statusCode == 500) client.println(" Internal Server Error");
  else client.println();

  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(formattedBody.length());
  client.println();
  client.print(formattedBody);
}

void sendHttpHtml(WiFiClient &client, int statusCode, const String &body) {
  client.print("HTTP/1.1 ");
  client.print(statusCode);
  if (statusCode == 200) client.println(" OK");
  else if (statusCode == 404) client.println(" Not Found");
  else client.println();

  client.println("Content-Type: text/html; charset=utf-8");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.print(body);
}

String endpointIndexHtml() {
  String body;
  body.reserve(1024);

  body += "<!doctype html><html><head><meta charset=\"utf-8\">";
  body += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  body += "<title>Lake Logger</title>";
  body += "<style>body{font-family:Arial,sans-serif;margin:2rem;line-height:1.5;}";
  body += "a,button{font-size:1rem;}ul{padding-left:1.2rem;}li{margin:.7rem 0;}";
  body += "button{padding:.5rem .8rem;cursor:pointer;}code{background:#f3f3f3;padding:.1rem .3rem;border-radius:4px;}";
  body += "</style></head><body>";
  body += "<h1>Solinst Lake Logger</h1>";
  body += "<p>Available endpoints:</p><ul>";
  body += "<li><a href=\"/status\">/status</a> - cached device status JSON</li>";
  body += "<li><a href=\"/probe\">/probe</a> - trigger a live probe and return JSON</li>";
  body += "<li><button onclick=\"confirmReset()\">/reset</button> - reboot the device</li>";
  body += "</ul>";
  body += "<script>function confirmReset(){if(confirm('Reset the lake logger now?')){window.location='/reset';}}</script>";
  body += "</body></html>";
  return body;
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
  const ProbeReading &powerSnapshot = lastProbeReading;
  float batteryChargePct = approximateBatteryChargePercent(powerSnapshot);
  bool solarChargingNow = solarChargingBatteryNow(powerSnapshot);
  unsigned long uploadCooldownRemainingMs = (millis() < nextUploadAllowedMs) ? (nextUploadAllowedMs - millis()) : 0UL;

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
  body += "\"last_upload_error_utc\":\"" + jsonEscape(lastUploadErrorUtc) + "\",";
  body += "\"last_upload_error\":\"" + jsonEscape(lastUploadError) + "\",";
  body += "\"upload_cooldown_remaining_ms\":" + String(uploadCooldownRemainingMs) + ",";
  body += "\"consecutive_upload_failures\":" + String(consecutiveUploadFailures) + ",";
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

  Serial.println("HTTP: client connected");

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

  Serial.print("HTTP: request line = ");
  Serial.println(requestLine);

  if (requestLine.startsWith("GET /probe")) {
    Serial.println("HTTP: handling /probe");
    ProbeReading r;
    if (probeNow(r)) {
      Serial.println("HTTP: /probe success, sending 200");
      sendHttpJson(client, 200, probeJson(r));
    } else {
      Serial.println("HTTP: /probe failed, sending 500");
      sendHttpJson(client, 500, "{\"ok\":false,\"error\":\"probe read failed\"}");
    }
  } else if (requestLine.startsWith("GET /status")) {
    Serial.println("HTTP: handling /status");
    sendHttpJson(client, 200, statusJson());
    Serial.println("HTTP: /status sent");
  } else if (requestLine.startsWith("GET /reset")) {
    Serial.println("HTTP: handling /reset");
    sendHttpJson(client, 200, "{\"ok\":true,\"message\":\"rebooting\"}");
    client.flush();
    delay(200);
    NVIC_SystemReset();
  } else {
    Serial.println("HTTP: route not found, sending HTML index");
    sendHttpHtml(client, 404, endpointIndexHtml());
  }

  delay(1);
  client.stop();
  Serial.println("HTTP: client closed");
}
