#include "config.h"

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected, IP=");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("Wi-Fi connection failed");
  return false;
}

bool syncClockFromNtp() {
  if (!connectWiFi()) {
    clockValid = isClockValid();
    logger_core::recordNtpSyncResult(
        ntpSyncState, millis(), false, clockValid, NTP_RESYNC_INTERVAL_MS,
        NTP_INVALID_CLOCK_RETRY_INITIAL_MS, NTP_INVALID_CLOCK_RETRY_MAX_MS,
        NTP_VALID_CLOCK_RETRY_INITIAL_MS, NTP_VALID_CLOCK_RETRY_MAX_MS);
    Serial.print("NTP sync deferred after Wi-Fi failure; retry in ms: ");
    Serial.println(logger_core::ntpCooldownRemaining(millis(), ntpSyncState));
    return false;
  }

  timeClient.begin();

  const unsigned long requestMs = millis();
  const bool updateExpectedToRequest =
      !timeClient.isTimeSet() ||
      (requestMs - lastNtpSyncMs >= NTP_CLIENT_UPDATE_INTERVAL_MS);
  bool gotTime = timeClient.update();
  if (!gotTime && !updateExpectedToRequest) {
    gotTime = timeClient.forceUpdate();
  }

  if (!gotTime) {
    clockValid = isClockValid();
    logger_core::recordNtpSyncResult(
        ntpSyncState, millis(), false, clockValid, NTP_RESYNC_INTERVAL_MS,
        NTP_INVALID_CLOCK_RETRY_INITIAL_MS, NTP_INVALID_CLOCK_RETRY_MAX_MS,
        NTP_VALID_CLOCK_RETRY_INITIAL_MS, NTP_VALID_CLOCK_RETRY_MAX_MS);
    Serial.print("NTP sync failed; consecutive failures: ");
    Serial.print(ntpSyncState.consecutiveFailures);
    Serial.print(", retry in ms: ");
    Serial.println(logger_core::ntpCooldownRemaining(millis(), ntpSyncState));
    return false;
  }

  unsigned long epoch = timeClient.getEpochTime();
  set_time(epoch);
  lastNtpSyncMs = millis();

  clockValid = isClockValid();
  logger_core::recordNtpSyncResult(
      ntpSyncState, lastNtpSyncMs, clockValid, clockValid,
      NTP_RESYNC_INTERVAL_MS, NTP_INVALID_CLOCK_RETRY_INITIAL_MS,
      NTP_INVALID_CLOCK_RETRY_MAX_MS, NTP_VALID_CLOCK_RETRY_INITIAL_MS,
      NTP_VALID_CLOCK_RETRY_MAX_MS);

  Serial.print("Clock synced from NTP: ");
  Serial.println(epochToIso8601UTC(epoch));
  Serial.print("Clock valid: ");
  Serial.println(clockValid ? "true" : "false");

  return clockValid;
}

bool isClockValid() {
  time_t now = time(NULL);

  // 2024-01-01T00:00:00Z
  constexpr time_t MIN_VALID_EPOCH = 1704067200;

  return now >= MIN_VALID_EPOCH;
}

void maintainClockSync() {
  clockValid = isClockValid();
  if (logger_core::ntpSyncDue(millis(), ntpSyncState)) {
    syncClockFromNtp();
  }
}

String nowUtcString() {
  time_t now = time(NULL);

  if (!isClockValid()) {
    clockValid = false;
    return "";
  }

  clockValid = true;
  return epochToIso8601UTC((unsigned long)now);
}

bool shouldLogNow() {
  time_t now = time(NULL);
  const logger_core::LogScheduleDecision decision =
      logger_core::observeUtcLogSchedule(
          isClockValid(), static_cast<int64_t>(now), LOG_INTERVAL_MINUTES,
          LOG_BOUNDARY_WINDOW_SECONDS, logScheduleState);
  return decision != logger_core::LogScheduleDecision::NOT_DUE;
}