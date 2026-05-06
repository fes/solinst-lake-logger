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
    return false;
  }

  timeClient.begin();

  bool gotTime = false;
  for (int i = 0; i < 10; i++) {
    if (timeClient.update()) {
      gotTime = true;
      break;
    }
    timeClient.forceUpdate();
    delay(500);
  }

  if (!gotTime) {
    Serial.println("NTP sync failed");
    return false;
  }

  unsigned long epoch = timeClient.getEpochTime();
  set_time(epoch);
  lastNtpSyncMs = millis();

  clockValid = isClockValid();

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
  if (!isClockValid() || (millis() - lastNtpSyncMs >= NTP_RESYNC_INTERVAL_MS)) {
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
  if (!isClockValid()) {
    return false;
  }

  time_t now = time(NULL);
  struct tm *utc = gmtime(&now);
  if (!utc) {
    return false;
  }

  bool onBoundary =
      (utc->tm_min % LOG_INTERVAL_MINUTES == 0) &&
      (utc->tm_sec < LOG_BOUNDARY_WINDOW_SECONDS);

  if (!onBoundary) {
    return false;
  }

  if (utc->tm_year != lastLoggedTmYear ||
      utc->tm_yday != lastLoggedTmYDay ||
      utc->tm_hour != lastLoggedTmHour ||
      utc->tm_min  != lastLoggedTmMin) {
    lastLoggedTmYear = utc->tm_year;
    lastLoggedTmYDay = utc->tm_yday;
    lastLoggedTmHour = utc->tm_hour;
    lastLoggedTmMin  = utc->tm_min;
    return true;
  }

  return false;
}