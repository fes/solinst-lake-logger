#include "config.h"

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

const char* httpReasonPhrase(int statusCode) {
  switch (statusCode) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 414: return "URI Too Long";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    default: return "";
  }
}

bool writeHttpBytes(
    WiFiClient& client, const uint8_t* data, size_t length) {
  const unsigned long startedMs = millis();
  size_t offset = 0;
  while (offset < length && millis() - startedMs < 3000UL) {
    const size_t chunk =
        min(static_cast<size_t>(512), length - offset);
    const size_t written = client.write(data + offset, chunk);
    if (written > 0) {
      offset += written;
    } else {
      if (!client.connected()) break;
      delay(1);
    }
  }
  return offset == length;
}

bool writeHttpString(WiFiClient& client, const String& value) {
  return writeHttpBytes(
      client, reinterpret_cast<const uint8_t*>(value.c_str()),
      value.length());
}

void closeHttpClient(WiFiClient& client) {
  // Mbed close() can discard queued response data on higher-latency Wi-Fi.
  delay(200);
  client.stop();
}

void sendHttpJson(WiFiClient &client, int statusCode, const String &body,
                  const char* extraHeader) {
  String formattedBody = prettyJson(body);
  String headers = "HTTP/1.1 ";
  headers += statusCode;
  headers += " ";
  headers += httpReasonPhrase(statusCode);
  headers += "\r\nContent-Type: application/json\r\nConnection: close\r\n";
  if (extraHeader != nullptr) {
    headers += extraHeader;
    headers += "\r\n";
  }
  headers += "Content-Length: ";
  headers += formattedBody.length();
  headers += "\r\n\r\n";
  if (!writeHttpString(client, headers) ||
      !writeHttpString(client, formattedBody)) {
    Serial.println("HTTP: response write incomplete");
  }
}

void sendHttpHtml(WiFiClient &client, int statusCode, const String &body) {
  String headers = "HTTP/1.1 ";
  headers += statusCode;
  headers += " ";
  headers += httpReasonPhrase(statusCode);
  headers +=
      "\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\n"
      "Content-Length: ";
  headers += body.length();
  headers += "\r\n\r\n";
  if (!writeHttpString(client, headers) || !writeHttpString(client, body)) {
    Serial.println("HTTP: response write incomplete");
  }
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
  const logger_core::SiteSnapshot siteSnapshot = currentSiteSnapshot();
  ProbeReading cachedProbeSnapshot = lastProbeReading;
  ProbeReading livePowerSnapshot;
  copyLatestPowerSnapshotToReading(livePowerSnapshot);
  // Keep diagnostics responsive while periodic weather polling handles retries.
  WeatherReading liveWeatherSnapshot = lastWeatherReading;
  float batteryChargePct = approximateBatteryChargePercent(livePowerSnapshot);
  bool solarChargingNow = solarChargingBatteryNow(livePowerSnapshot);
  unsigned long uploadCooldownRemainingMs =
      logger_core::deadlinePending(millis(), nextUploadAllowedMs)
          ? (nextUploadAllowedMs - millis())
          : 0UL;
  unsigned long ntpCooldownRemainingMs =
      logger_core::ntpCooldownRemaining(millis(), ntpSyncState);
  unsigned long sensorDiscoveryCooldownRemainingMs =
      logger_core::sensorDiscoveryCooldownRemaining(
          millis(), detectedSensorId != 0, sensorDiscoveryState);
  unsigned long displayWakeRemainingMs = (displayAwake && (long)(displayWakeUntilMs - millis()) > 0) ? (displayWakeUntilMs - millis()) : 0UL;

  String body = "{";
  body += "\"board_profile\":\"" +
          jsonEscape(String(ACTIVE_BOARD_PROFILE.name)) + "\",";
  body += "\"site_health\":\"" +
          String(logger_core::siteHealthName(siteSnapshot.health)) + "\",";
  body += "\"rs485_channel_count\":" +
          String(ACTIVE_BOARD_PROFILE.rs485ChannelCount) + ",";
  body += "\"display_behavior\":\"" +
          String(
              ACTIVE_BOARD_PROFILE.displayBehavior == DisplayBehavior::HEADLESS
                  ? "headless"
              : ACTIVE_BOARD_PROFILE.displayBehavior ==
                        DisplayBehavior::PERSISTENT_EPAPER
                  ? "persistent_epaper"
                  : "wake_on_demand") + "\",";
  body += "\"device_id\":\"" + jsonEscape(String(DEVICE_ID)) + "\",";
  body += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  body += "\"http_server_started\":" + String(httpServerStarted ? "true" : "false") + ",";
  body += "\"wifi_rssi_dbm\":" + String((WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0) + ",";
  body += "\"ip\":\"" + jsonEscape(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("")) + "\",";
  body += "\"uptime_s\":" + String((millis() - bootMs) / 1000UL) + ",";
  body += "\"clock_valid\":" + String(clockValid ? "true" : "false") + ",";
  body += "\"clock_valid_check\":" + String(isClockValid() ? "true" : "false") + ",";
  body += "\"clock_now_utc\":\"" + jsonEscape(nowUtcString()) + "\",";
  body += "\"last_ntp_sync_age\":\"" + jsonEscape(millisAgeString(lastNtpSyncMs)) + "\",";
  body += "\"ntp_attempted\":" + String(ntpSyncState.attempted ? "true" : "false") + ",";
  body += "\"last_ntp_attempt_succeeded\":" + String(ntpSyncState.lastAttemptSucceeded ? "true" : "false") + ",";
  body += "\"last_ntp_attempt_age\":\"" + jsonEscape(ntpSyncState.attempted ? millisAgeString(ntpSyncState.lastAttemptMs) : String("never")) + "\",";
  body += "\"ntp_cooldown_remaining_ms\":" + String(ntpCooldownRemainingMs) + ",";
  body += "\"consecutive_ntp_failures\":" + String(ntpSyncState.consecutiveFailures) + ",";
  body += "\"upload_endpoint_mode\":\"" + jsonEscape(String(uploadEndpointModeName())) + "\",";
  body += "\"upload_endpoint_host\":\"" + jsonEscape(String(uploadEndpointHost())) + "\",";
  body += "\"upload_endpoint_path\":\"" + jsonEscape(String(uploadEndpointPath())) + "\",";
  body += "\"upload_endpoint_port\":" + String(uploadEndpointPort()) + ",";
  body += "\"upload_endpoint_https\":" + String(uploadEndpointUsesHttps() ? "true" : "false") + ",";
  body += "\"sensor_found\":" + String(detectedSensorId != 0 ? "true" : "false") + ",";
  body += "\"modbus_id\":" + String(detectedSensorId) + ",";
  body += "\"configured_fixed_modbus_id_enabled\":" + String(WLTS_USE_FIXED_MODBUS_ID ? "true" : "false") + ",";
  body += "\"configured_fixed_modbus_id\":" + String(WLTS_FIXED_MODBUS_ID) + ",";
  body += "\"sensor_discovery_attempted\":" + String(sensorDiscoveryState.attempted ? "true" : "false") + ",";
  body += "\"sensor_discovery_attempt_count\":" + String(sensorDiscoveryState.attemptCount) + ",";
  body += "\"last_sensor_discovery_attempt_succeeded\":" + String(sensorDiscoveryState.lastAttemptSucceeded ? "true" : "false") + ",";
  body += "\"last_sensor_discovery_attempt_age\":\"" + jsonEscape(sensorDiscoveryState.attempted ? millisAgeString(sensorDiscoveryState.lastAttemptMs) : String("never")) + "\",";
  body += "\"sensor_discovery_cooldown_remaining_ms\":" + String(sensorDiscoveryCooldownRemainingMs) + ",";
  body += "\"consecutive_sensor_discovery_failures\":" + String(sensorDiscoveryState.consecutiveFailures) + ",";
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
  body += "\"display_recovery_count\":" + String(displayI2cRecoveryCount()) + ",";
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
  body += "\"last_permanent_upload_rejection_status\":" + String(lastPermanentUploadRejectionStatus) + ",";
  body += "\"last_permanent_upload_rejection_error\":\"" + jsonEscape(lastPermanentUploadRejectionError) + "\",";
  body += "\"last_permanent_upload_rejection_utc\":\"" + jsonEscape(lastPermanentUploadRejectionUtc) + "\",";
  body += "\"last_permanent_upload_rejected_reading_utc\":\"" + jsonEscape(lastPermanentUploadRejectedReadingUtc) + "\",";
  body += "\"upload_cooldown_remaining_ms\":" + String(uploadCooldownRemainingMs) + ",";
  body += "\"consecutive_upload_failures\":" + String(consecutiveUploadFailures) + ",";
  body += "\"successful_probe_reads\":" + String(successfulProbeReads) + ",";
  body += "\"failed_probe_reads\":" + String(failedProbeReads) + ",";
  body += "\"successful_uploads\":" + String(successfulUploads) + ",";
  body += "\"failed_uploads\":" + String(failedUploads) + ",";
  body += "\"permanent_upload_rejections\":" + String(permanentUploadRejections) + ",";
  body += "\"permanent_backlog_drops\":" + String(permanentBacklogDrops) + ",";
  body += "\"backlog_count\":" + String(backlogStorage.count()) + ",";
  body += "\"dropped_backlog_entries\":" + String(droppedBacklogEntries) + ",";
  body += "\"power_monitor_init_status\":\"" + jsonEscape(powerMonitorInitStatus) + "\",";
  body += "\"power_snapshot_initialized\":" + String(powerPollState.polled ? "true" : "false") + ",";
  body += "\"power_snapshot_age\":\"" + jsonEscape(powerPollState.polled ? millisAgeString(powerPollState.lastPollMs) : String("never")) + "\",";
  body += "\"power_poll_interval_ms\":" + String(POWER_MONITOR_POLL_INTERVAL_MS) + ",";
  body += "\"battery_voltage_24h_min_v\":";
  body += siteSnapshot.batteryExtrema24hValid
              ? String(siteSnapshot.batteryVoltageMin24hV, 3)
              : String("null");
  body += ",\"battery_voltage_24h_max_v\":";
  body += siteSnapshot.batteryExtrema24hValid
              ? String(siteSnapshot.batteryVoltageMax24hV, 3)
              : String("null");
  body += ",";
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

logger_core::RequestLineReadResult readRequestLine(
    WiFiClient &client, logger_core::RequestLineReader &reader,
    bool &receivedByte) {
  unsigned long start = millis();
  receivedByte = false;

  while (millis() - start < 2000UL) {
    while (client.available()) {
      receivedByte = true;
      const logger_core::RequestLineReadResult result =
          logger_core::consumeRequestLineByte(reader, client.read());
      if (result != logger_core::RequestLineReadResult::IN_PROGRESS) {
        return result;
      }
    }
    delay(10);
  }
  return logger_core::finishRequestLine(reader);
}

logger_core::HeaderParseResult readHttpHeaders(WiFiClient &client) {
  unsigned long start = millis();
  logger_core::HeaderParser parser;

  while (millis() - start < 4000UL) {
    while (client.available()) {
      const logger_core::HeaderParseResult result =
          logger_core::consumeHeaderByte(parser, client.read());
      if (result != logger_core::HeaderParseResult::IN_PROGRESS) {
        return result;
      }
    }
    delay(1);
  }
  return logger_core::finishHeaders(parser);
}

void sendHttpError(WiFiClient &client, int statusCode,
                   const char* message, const char* extraHeader = nullptr) {
  String body = "{\"ok\":false,\"error\":\"";
  body += message;
  body += "\"}";
  sendHttpJson(client, statusCode, body, extraHeader);
}

void handleHttpClient() {
  WiFiClient client = server.accept();
  if (!client) return;
  Serial.println("HTTP: client connected");

  logger_core::RequestLineReader requestLineReader;
  bool receivedRequestByte = false;
  const logger_core::RequestLineReadResult readResult =
      readRequestLine(client, requestLineReader, receivedRequestByte);
  if (!receivedRequestByte) {
    Serial.println("HTTP: empty request line, closing client");
    closeHttpClient(client);
    return;
  }
  if (readResult == logger_core::RequestLineReadResult::TOO_LONG) {
    sendHttpError(client, 414, "request line too long");
    closeHttpClient(client);
    return;
  }
  if (readResult != logger_core::RequestLineReadResult::COMPLETE) {
    sendHttpError(client, 400, "malformed request line");
    closeHttpClient(client);
    return;
  }

  logger_core::HttpRequest request;
  const logger_core::HttpRequestParseResult parseResult =
      logger_core::parseHttpRequestLine(
          requestLineReader.line, requestLineReader.length, request);
  if (parseResult == logger_core::HttpRequestParseResult::URI_TOO_LONG) {
    sendHttpError(client, 414, "request target too long");
    closeHttpClient(client);
    return;
  }
  if (parseResult != logger_core::HttpRequestParseResult::OK) {
    sendHttpError(client, 400, "malformed request line");
    closeHttpClient(client);
    return;
  }

  const logger_core::HeaderParseResult headerResult = readHttpHeaders(client);
  if (headerResult == logger_core::HeaderParseResult::TOO_LARGE) {
    sendHttpError(client, 431, "request headers too large");
    closeHttpClient(client);
    return;
  }
  if (headerResult != logger_core::HeaderParseResult::COMPLETE) {
    sendHttpError(client, 400, "malformed request headers");
    closeHttpClient(client);
    return;
  }

  Serial.print("HTTP: request line = ");
  Serial.println(requestLineReader.line);
  const logger_core::HttpRouteDecision routeDecision =
      logger_core::routeHttpRequest(request);
  if (routeDecision == logger_core::HttpRouteDecision::METHOD_NOT_ALLOWED) {
    Serial.println("HTTP: method not allowed");
    sendHttpError(client, 405, "method not allowed", "Allow: GET");
  } else if (routeDecision == logger_core::HttpRouteDecision::INDEX) {
    Serial.println("HTTP: handling /");
    sendHttpHtml(client, 200, endpointIndexHtml());
  } else if (routeDecision == logger_core::HttpRouteDecision::PROBE) {
    Serial.println("HTTP: handling /probe");
    ProbeReading r;
    if (probeNow(r)) {
      Serial.println("HTTP: /probe success, sending 200");
      sendHttpJson(client, 200, probeJson(r));
    } else {
      Serial.println("HTTP: /probe failed, sending 500");
      sendHttpJson(client, 500, "{\"ok\":false,\"error\":\"probe read failed\"}");
    }
  } else if (routeDecision == logger_core::HttpRouteDecision::STATUS) {
    Serial.println("HTTP: handling /status");
    sendHttpJson(client, 200, statusJson());
    Serial.println("HTTP: /status sent");
  } else if (routeDecision == logger_core::HttpRouteDecision::RESET) {
    Serial.println("HTTP: handling /reset");
    sendHttpJson(client, 200, "{\"ok\":true,\"message\":\"rebooting\"}");
    client.flush();
    delay(200);
    NVIC_SystemReset();
  } else {
    Serial.println("HTTP: route not found, sending HTML index");
    sendHttpHtml(client, 404, endpointIndexHtml());
  }
  closeHttpClient(client);
  Serial.println("HTTP: client closed");
}
