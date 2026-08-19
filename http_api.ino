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
      if (escaping) escaping = false;
      else if (c == '\\') escaping = true;
      else if (c == '"') inString = false;
      continue;
    }
    switch (c) {
      case '"': inString = true; out += c; break;
      case '{':
      case '[':
        out += c; out += '\n'; indent++; for (int j = 0; j < indent; j++) out += "  "; break;
      case '}':
      case ']':
        out += '\n'; indent = max(0, indent - 1); for (int j = 0; j < indent; j++) out += "  "; out += c; break;
      case ',':
        out += c; out += '\n'; for (int j = 0; j < indent; j++) out += "  "; break;
      case ':': out += ": "; break;
      default: if (c != '\n' && c != '\r' && c != '\t') out += c; break;
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
  else if (statusCode == 400) client.println(" Bad Request");
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
  body += "<style>body{font-family:Arial,sans-serif;margin:2rem;line-height:1.5;}a,button{font-size:1rem;}ul{padding-left:1.2rem;}li{margin:.7rem 0;}button{padding:.5rem .8rem;cursor:pointer;}code{background:#f3f3f3;padding:.1rem .3rem;border-radius:4px;}</style></head><body>";
  body += "<h1>Solinst Lake Logger</h1><p>Available endpoints:</p><ul>";
  body += "<li><a href=\"/status\">/status</a> - cached device status JSON</li>";
  body += "<li><a href=\"/probe\">/probe</a> - trigger a live probe and return JSON</li>";
  body += "<li><button onclick=\"confirmReset()\">/reset</button> - reboot the device</li>";
  body += "</ul><script>function confirmReset(){if(confirm('Reset the lake logger now?')){window.location='/reset';}}</script></body></html>";
  return body;
}

String probeJson(const ProbeReading &r) {
  float batteryChargePct = approximateBatteryChargePercent(r);
  bool solarChargingNow = solarChargingBatteryNow(r);
  String body = "{";
  body += "\"ok\":" + String(r.valid ? "true" : "false") + ",";
  body += "\"timestamp_utc\":\"" + jsonEscape(r.timestampUtc) + "\",";
  body += "\"water_level_m\":" + (r.valid ? String(r.level, 4) : String("null")) + ",";
  body += "\"temperature_c\":" + (r.valid ? String(r.temperature, 3) : String("null")) + ",";
  appendPowerMonitorJson(body, "battery_output", r.batteryOutput);
  appendPowerMonitorJson(body, "solar_input", r.solarInput);
  appendWeatherJson(body, "weather", r.weather);
  body += "\"battery_charge_level_pct_approx\":";
  body += (isfinite(batteryChargePct) ? String(batteryChargePct, 1) : String("null"));
  body += ",";
  body += "\"solar_charging_battery\":" + String(solarChargingNow ? "true" : "false") + ",";
  body += "\"units\":{\"level\":\"m\",\"temperature\":\"C\"}";
  body += "}";
  return body;
}

String statusJson() {
  ProbeReading cachedProbeSnapshot = lastProbeReading;
  ProbeReading livePowerSnapshot = lastProbeReading;
  readPowerMonitors(livePowerSnapshot);
  // Keep diagnostics responsive while periodic weather polling handles retries.
  WeatherReading liveWeatherSnapshot = lastWeatherReading;
  float batteryChargePct = approximateBatteryChargePercent(livePowerSnapshot);
  bool solarChargingNow = solarChargingBatteryNow(livePowerSnapshot);
  unsigned long uploadCooldownRemainingMs = ((long)(nextUploadAllowedMs - millis()) > 0) ? (nextUploadAllowedMs - millis()) : 0UL;
  unsigned long displayWakeRemainingMs = (displayAwake && (long)(displayWakeUntilMs - millis()) > 0) ? (displayWakeUntilMs - millis()) : 0UL;

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
  body += "\"upload_endpoint_mode\":\"" + jsonEscape(String(uploadEndpointModeName())) + "\",";
  body += "\"upload_endpoint_host\":\"" + jsonEscape(String(uploadEndpointHost())) + "\",";
  body += "\"upload_endpoint_path\":\"" + jsonEscape(String(uploadEndpointPath())) + "\",";
  body += "\"upload_endpoint_port\":" + String(uploadEndpointPort()) + ",";
  body += "\"upload_endpoint_https\":" + String(uploadEndpointUsesHttps() ? "true" : "false") + ",";
  body += "\"sensor_found\":" + String(detectedSensorId != 0 ? "true" : "false") + ",";
  body += "\"modbus_id\":" + String(detectedSensorId) + ",";
  body += "\"configured_fixed_modbus_id_enabled\":" + String(WLTS_USE_FIXED_MODBUS_ID ? "true" : "false") + ",";
  body += "\"configured_fixed_modbus_id\":" + String(WLTS_FIXED_MODBUS_ID) + ",";
  body += "\"serial_number\":\"" + String(detectedIdentity.serialNumber) + "\",";
  body += "\"firmware\":\"" + String(detectedIdentity.fwMajor) + "." + String(detectedIdentity.fwMinor) + "\",";
  body += "\"display_present\":" + String(displayPresent ? "true" : "false") + ",";
  body += "\"display_awake\":" + String(displayAwake ? "true" : "false") + ",";
  body += "\"display_wake_remaining_ms\":" + String(displayWakeRemainingMs) + ",";
  body += "\"last_display_wake_request_utc\":\"" + jsonEscape(lastDisplayWakeRequestUtc()) + "\",";
  body += "\"last_display_wake_request_age\":\"" + jsonEscape(lastDisplayWakeRequestAge()) + "\",";
  body += "\"last_display_refresh_utc\":\"" + jsonEscape(lastDisplayRefreshUtc()) + "\",";
  body += "\"last_display_refresh_age\":\"" + jsonEscape(lastDisplayRefreshAge()) + "\",";
  body += "\"display_refresh_count\":" + String(displayRefreshCount()) + ",";
  body += "\"display_i2c_recovery_count\":" + String(displayI2cRecoveryCount()) + ",";
  body += "\"user_button_present\":" + String(userButtonPresent ? "true" : "false") + ",";
  body += "\"last_user_button_press_utc\":\"" + jsonEscape(lastUserButtonPressUtc) + "\",";
  body += "\"last_user_button_press_age\":\"" + jsonEscape(millisAgeString(lastUserButtonPressMs)) + "\",";
  body += "\"last_successful_probe_read_utc\":\"" + jsonEscape(lastSuccessfulProbeReadUtc) + "\",";
  body += "\"last_successful_probe_read_age\":\"" + jsonEscape(millisAgeString(lastSuccessfulProbeReadMs)) + "\",";
  body += "\"last_successful_battery_output_read_utc\":\"" + jsonEscape(lastSuccessfulBatteryOutputReadUtc) + "\",";
  body += "\"last_successful_battery_output_read_age\":\"" + jsonEscape(millisAgeString(lastSuccessfulBatteryOutputReadMs)) + "\",";
  body += "\"last_successful_solar_input_read_utc\":\"" + jsonEscape(lastSuccessfulSolarInputReadUtc) + "\",";
  body += "\"last_successful_solar_input_read_age\":\"" + jsonEscape(millisAgeString(lastSuccessfulSolarInputReadMs)) + "\",";
  body += "\"last_successful_weather_read_utc\":\"" + jsonEscape(lastSuccessfulWeatherReadUtc) + "\",";
  body += "\"last_successful_weather_read_age\":\"" + jsonEscape(millisAgeString(lastSuccessfulWeatherReadMs)) + "\",";
  body += "\"weather_sensor_init_status\":\"" + jsonEscape(weatherSensorInitStatus) + "\",";
  body += "\"last_weather_error_utc\":\"" + jsonEscape(lastWeatherErrorUtc) + "\",";
  body += "\"last_weather_error\":\"" + jsonEscape(lastWeatherError) + "\",";
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
  body += "\"cached_probe_valid\":" + String(cachedProbeSnapshot.valid ? "true" : "false") + ",";
  appendPowerMonitorJson(body, "cached_probe_battery_output", cachedProbeSnapshot.batteryOutput);
  appendPowerMonitorJson(body, "cached_probe_solar_input", cachedProbeSnapshot.solarInput);
  appendWeatherJson(body, "cached_probe_weather", cachedProbeSnapshot.weather);
  appendPowerMonitorJson(body, "live_battery_output", livePowerSnapshot.batteryOutput);
  appendPowerMonitorJson(body, "live_solar_input", livePowerSnapshot.solarInput);
  appendWeatherJson(body, "live_weather", liveWeatherSnapshot);
  body += "\"battery_charge_level_pct_approx\":";
  body += (isfinite(batteryChargePct) ? String(batteryChargePct, 1) : String("null"));
  body += ",";
  body += "\"solar_charging_battery\":" + String(solarChargingNow ? "true" : "false");
  body += "}";
  return body;
}

bool readRequestLine(WiFiClient &client, String &requestLine) {
  unsigned long start = millis();
  requestLine = "";

  while (client.connected() && millis() - start < 2000UL) {
    while (client.available()) {
      char c = client.read();
      if (c == '\r') continue;
      if (c == '\n') {
        requestLine.trim();
        if (requestLine.length() == 0) {
          continue;
        }
        return true;
      }
      requestLine += c;
      if (requestLine.length() > 200) {
        requestLine.trim();
        return requestLine.length() > 0;
      }
    }
    delay(1);
  }

  requestLine.trim();
  return requestLine.length() > 0;
}

void drainHttpHeaders(WiFiClient &client) {
  unsigned long start = millis();
  String line = "";

  while (client.connected() && millis() - start < 4000UL) {
    while (client.available()) {
      char c = client.read();
      if (c == '\r') continue;
      if (c == '\n') {
        if (line.length() == 0) {
          return;
        }
        line = "";
      } else {
        line += c;
      }
    }
    delay(1);
  }
}

void handleHttpClient() {
  WiFiClient client = server.available();
  if (!client) return;
  Serial.println("HTTP: client connected");

  String requestLine = "";
  if (!readRequestLine(client, requestLine)) {
    Serial.println("HTTP: empty request line, closing client");
    client.stop();
    return;
  }

  drainHttpHeaders(client);

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
