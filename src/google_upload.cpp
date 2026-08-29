#include "config.h"

String makePayload(const ProbeReading &r) {
  float batteryChargePct = approximateBatteryChargePercent(r);
  bool solarChargingNow = solarChargingBatteryNow(r);

  String payload = "{";
  payload += "\"secret\":\"" + jsonEscape(String(SHARED_SECRET)) + "\",";
  payload += "\"device_id\":\"" + jsonEscape(String(DEVICE_ID)) + "\",";
  payload += "\"timestamp_utc\":\"" + jsonEscape(r.timestampUtc) + "\",";
  payload += "\"modbus_id\":" + String(r.modbusId) + ",";
  payload += "\"serial_number\":\"" + String(r.sensorIdentity.serialNumber) + "\",";
  payload += "\"firmware\":\"" + String(r.sensorIdentity.fwMajor) + "." + String(r.sensorIdentity.fwMinor) + "\",";
  payload += "\"water_level_m\":" + String(r.level, 4) + ",";
  payload += "\"temperature_c\":" + String(r.temperature, 3) + ",";

  appendPowerMonitorJson(payload, "battery_output", r.batteryOutput);
  appendPowerMonitorJson(payload, "solar_input", r.solarInput);
  appendWeatherJson(payload, "weather", r.weather);

  payload += "\"battery_charge_level_pct_approx\":";
  payload += (isfinite(batteryChargePct) ? String(batteryChargePct, 1) : String("null"));
  payload += ",";
  payload += "\"solar_charging_battery\":" + String(solarChargingNow ? "true" : "false") + ",";

  payload += "\"status\":\"OK\"";
  payload += "}";
  return payload;
}

void recordUploadFailure(const String &reason) {
  lastUploadError = reason;
  lastUploadErrorUtc = nowUtcString();

  logger_core::UploadCooldownState state{
      consecutiveUploadFailures, nextUploadAllowedMs};
  logger_core::recordUploadOperationOutcome(
      state, millis(), UploadOutcome::TRANSIENT_FAILURE,
      UPLOAD_RETRY_COOLDOWN_INITIAL_MS,
      UPLOAD_RETRY_COOLDOWN_MAX_MS);
  consecutiveUploadFailures = state.consecutiveFailures;
  nextUploadAllowedMs = state.nextAllowedMs;

  Serial.print("Upload cooldown set to ms: ");
  Serial.println(logger_core::exponentialBackoff(
      UPLOAD_RETRY_COOLDOWN_INITIAL_MS,
      UPLOAD_RETRY_COOLDOWN_MAX_MS,
      consecutiveUploadFailures));
  Serial.print("Upload failure reason: ");
  Serial.println(reason);
}

void clearUploadFailureState() {
  logger_core::UploadCooldownState state{
      consecutiveUploadFailures, nextUploadAllowedMs};
  logger_core::recordUploadOperationOutcome(
      state, millis(), UploadOutcome::ACCEPTED,
      UPLOAD_RETRY_COOLDOWN_INITIAL_MS,
      UPLOAD_RETRY_COOLDOWN_MAX_MS);
  consecutiveUploadFailures = state.consecutiveFailures;
  nextUploadAllowedMs = state.nextAllowedMs;
  lastUploadError = "";
  lastUploadErrorUtc = "";
}

void recordPermanentUploadRejection(const UploadResult &result,
                                    const ProbeReading &reading) {
  if (permanentUploadRejections != UINT32_MAX) {
    ++permanentUploadRejections;
  }
  lastPermanentUploadRejectionStatus = result.statusCode;
  lastPermanentUploadRejectionError = result.error;
  lastPermanentUploadRejectionUtc = nowUtcString();
  lastPermanentUploadRejectedReadingUtc = reading.timestampUtc;

  logger_core::UploadCooldownState state{
      consecutiveUploadFailures, nextUploadAllowedMs};
  logger_core::recordUploadOperationOutcome(
      state, millis(), UploadOutcome::PERMANENT_REJECTION,
      UPLOAD_RETRY_COOLDOWN_INITIAL_MS,
      UPLOAD_RETRY_COOLDOWN_MAX_MS);
  consecutiveUploadFailures = state.consecutiveFailures;
  nextUploadAllowedMs = state.nextAllowedMs;
  lastUploadError = "";
  lastUploadErrorUtc = "";

  Serial.println("ERROR: upload permanently rejected; reading will not retry");
  Serial.print("Rejected reading timestamp: ");
  Serial.println(reading.timestampUtc);
  Serial.print("Permanent rejection: ");
  Serial.println(result.error);
}

const char* uploadEndpointModeName() {
  return UPLOAD_ENDPOINT_MODE == UploadEndpointMode::GOOGLE_APPS_SCRIPT
           ? "google_apps_script"
           : "feslabs_ingest";
}

const char* uploadEndpointHost() {
  return UPLOAD_ENDPOINT_MODE == UploadEndpointMode::GOOGLE_APPS_SCRIPT
           ? GOOGLE_APPS_SCRIPT_HOST
           : FESLABS_INGEST_HOST;
}

const char* uploadEndpointPath() {
  return UPLOAD_ENDPOINT_MODE == UploadEndpointMode::GOOGLE_APPS_SCRIPT
           ? GOOGLE_APPS_SCRIPT_PATH
           : FESLABS_INGEST_PATH;
}

uint16_t uploadEndpointPort() {
  return UPLOAD_ENDPOINT_MODE == UploadEndpointMode::GOOGLE_APPS_SCRIPT
           ? GOOGLE_APPS_SCRIPT_PORT
           : FESLABS_INGEST_PORT;
}

bool uploadEndpointUsesHttps() {
  return UPLOAD_ENDPOINT_MODE == UploadEndpointMode::GOOGLE_APPS_SCRIPT
           ? true
           : FESLABS_INGEST_USE_HTTPS;
}

HttpClient& uploadHttpClient() {
  if (UPLOAD_ENDPOINT_MODE == UploadEndpointMode::GOOGLE_APPS_SCRIPT) {
    return googleAppsScriptHttpClient;
  }

  return FESLABS_INGEST_USE_HTTPS ? fesLabsHttpsClient : fesLabsHttpClient;
}

UploadResult postJson(const String &payload) {
  HttpClient &httpClient = uploadHttpClient();
  httpClient.stop();

  httpClient.beginRequest();
  httpClient.post(uploadEndpointPath());
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

  UploadResult result;
  result.statusCode = statusCode;
  result.outcome =
      logger_core::classifyUploadStatus(statusCode, UPLOAD_ENDPOINT_MODE);
  if (result.outcome == UploadOutcome::ACCEPTED) {
    if (UPLOAD_ENDPOINT_MODE == UploadEndpointMode::GOOGLE_APPS_SCRIPT &&
        statusCode >= 300 && statusCode < 400) {
      Serial.println(
          "Treating Google Apps Script redirect response as upload success");
    }
    return result;
  }

  result.error = String("HTTP ") + statusCode;
  if (response.length() > 0) {
    result.error += String(": ") + response;
  }
  return result;
}

UploadResult postReadingWithRetry(const ProbeReading &r) {
  unsigned long nowMs = millis();
  // Rollover-safe comparison: millis() wraps every ~49.7 days, and this
  // logger is expected to run unattended for months at a time.
  if (logger_core::deadlinePending(nowMs, nextUploadAllowedMs)) {
    Serial.print("Upload skipped due to cooldown; ms remaining: ");
    Serial.println(nextUploadAllowedMs - nowMs);
    return {UploadOutcome::DEFERRED, 0, "Upload cooldown active"};
  }

  if (!connectWiFi()) {
    Serial.println("Upload deferred: Wi-Fi not connected");
    return {UploadOutcome::DEFERRED, 0, "Wi-Fi not connected"};
  }

  lastUploadAttemptMs = millis();
  const String payload = makePayload(r);
  UploadResult result;

  for (int attempt = 1; attempt <= POST_RETRIES; attempt++) {
    result = postJson(payload);
    if (result.outcome == UploadOutcome::ACCEPTED) {
      lastSuccessfulUploadUtc = nowUtcString();
      lastSuccessfulUploadMs = millis();
      successfulUploads++;
      clearUploadFailureState();
      return result;
    }

    if (!logger_core::shouldRetryUpload(
            result.outcome, attempt, POST_RETRIES)) {
      break;
    }

    if (attempt < POST_RETRIES) {
      delay(POST_RETRY_DELAY_MS);
    }
  }

  if (result.outcome == UploadOutcome::PERMANENT_REJECTION) {
    recordPermanentUploadRejection(result, r);
  } else if (result.outcome == UploadOutcome::TRANSIENT_FAILURE) {
    if (failedUploads != UINT32_MAX) {
      ++failedUploads;
    }
    if (result.error.length() == 0) {
      result.error = "Upload failed after retries";
    }
    recordUploadFailure(result.error);
  }
  return result;
}

void flushBacklogOnce() {
  if (backlogStorage.count() == 0) return;
  if (logger_core::deadlinePending(millis(), nextUploadAllowedMs)) return;

  ProbeReading r;
  if (!peekBacklog(r)) return;

  const UploadResult result = postReadingWithRetry(r);
  switch (logger_core::backlogUploadAction(result.outcome)) {
    case logger_core::BacklogUploadAction::DEQUEUE: {
      ProbeReading uploaded;
      dequeueReading(uploaded);
      Serial.println("Uploaded one backlog reading");
      break;
    }
    case logger_core::BacklogUploadAction::DEQUEUE_REJECTED: {
      ProbeReading rejected;
      if (dequeueReading(rejected)) {
        if (permanentBacklogDrops != UINT32_MAX) {
          ++permanentBacklogDrops;
        }
        if (droppedBacklogEntries != UINT32_MAX) {
          ++droppedBacklogEntries;
        }
      }
      Serial.println(
          "ERROR: permanently rejected backlog reading dropped; FIFO advanced");
      break;
    }
    case logger_core::BacklogUploadAction::RETAIN:
      Serial.println("Backlog upload attempt failed or deferred; retaining head");
      break;
  }
}
