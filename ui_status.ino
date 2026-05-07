namespace {

#if defined(LED_D0) && defined(LED_D1) && defined(LED_D2) && defined(LED_D3)
constexpr pin_size_t HEARTBEAT_LED_PIN = LED_D0;
constexpr pin_size_t SENSOR_LED_PIN    = LED_D1;
constexpr pin_size_t NETWORK_LED_PIN   = LED_D2;
constexpr pin_size_t POWER_LED_PIN     = LED_D3;
constexpr bool UI_HAS_STATUS_LEDS = true;
#else
constexpr bool UI_HAS_STATUS_LEDS = false;
#endif

#if defined(BTN_USER)
constexpr pin_size_t USER_BUTTON_PIN = BTN_USER;
constexpr bool UI_HAS_USER_BUTTON = true;
#elif defined(USER_BUTTON)
constexpr pin_size_t USER_BUTTON_PIN = USER_BUTTON;
constexpr bool UI_HAS_USER_BUTTON = true;
#else
constexpr bool UI_HAS_USER_BUTTON = false;
#endif

bool ledPulse(unsigned long nowMs, unsigned long periodMs, unsigned long onMs) {
  return (nowMs % periodMs) < onMs;
}

bool ledDoublePulse(unsigned long nowMs, unsigned long periodMs, unsigned long firstOnMs, unsigned long gapMs, unsigned long secondOnMs) {
  unsigned long phase = nowMs % periodMs;
  if (phase < firstOnMs) return true;
  if (phase >= (firstOnMs + gapMs) && phase < (firstOnMs + gapMs + secondOnMs)) return true;
  return false;
}

void writeUiLed(pin_size_t pin, bool on) {
  digitalWrite(pin, on ? HIGH : LOW);
}

void updateHeartbeatLed(unsigned long nowMs) {
  writeUiLed(HEARTBEAT_LED_PIN, ledPulse(nowMs, 1000UL, 80UL));
}

void updateSensorLed(unsigned long nowMs) {
  if (detectedSensorId == 0) {
    writeUiLed(SENSOR_LED_PIN, ledPulse(nowMs, 1200UL, 120UL));
    return;
  }

  bool recentProbeSuccess = (lastSuccessfulProbeReadMs != 0) && ((nowMs - lastSuccessfulProbeReadMs) < (unsigned long)(LOG_INTERVAL_MINUTES * 60UL * 1000UL * 2UL));
  bool recentProbeFailure = (lastProbeAttemptMs != 0) && (lastProbeAttemptMs > lastSuccessfulProbeReadMs) && ((nowMs - lastProbeAttemptMs) < 30000UL);

  if (recentProbeFailure) {
    writeUiLed(SENSOR_LED_PIN, ledPulse(nowMs, 250UL, 80UL));
  } else if (recentProbeSuccess) {
    writeUiLed(SENSOR_LED_PIN, true);
  } else {
    writeUiLed(SENSOR_LED_PIN, ledPulse(nowMs, 700UL, 80UL));
  }
}

void updateNetworkLed(unsigned long nowMs) {
  if (WiFi.status() != WL_CONNECTED) {
    writeUiLed(NETWORK_LED_PIN, ledPulse(nowMs, 1000UL, 120UL));
    return;
  }

  bool recentUploadFailure = (lastUploadAttemptMs != 0) && (lastUploadAttemptMs > lastSuccessfulUploadMs) && ((nowMs - lastUploadAttemptMs) < 30000UL);
  bool recentUploadActivity = (lastUploadAttemptMs != 0) && ((nowMs - lastUploadAttemptMs) < 2000UL);

  if (backlogCount > 0 || recentUploadFailure) {
    writeUiLed(NETWORK_LED_PIN, ledDoublePulse(nowMs, 1200UL, 80UL, 120UL, 80UL));
  } else if (recentUploadActivity) {
    writeUiLed(NETWORK_LED_PIN, ledPulse(nowMs, 400UL, 80UL));
  } else {
    writeUiLed(NETWORK_LED_PIN, true);
  }
}

void updatePowerLed(unsigned long nowMs) {
  ProbeReading powerSnapshot = lastProbeReading;
  readPowerMonitors(powerSnapshot);

  float batteryPct = approximateBatteryChargePercent(powerSnapshot);
  bool charging = solarChargingBatteryNow(powerSnapshot);

  if (charging) {
    writeUiLed(POWER_LED_PIN, true);
  } else if (isfinite(batteryPct) && batteryPct < 20.0f) {
    writeUiLed(POWER_LED_PIN, ledPulse(nowMs, 500UL, 100UL));
  } else if (!powerSnapshot.batteryOutput.valid && !powerSnapshot.solarInput.valid) {
    writeUiLed(POWER_LED_PIN, ledDoublePulse(nowMs, 1500UL, 70UL, 120UL, 70UL));
  } else {
    writeUiLed(POWER_LED_PIN, false);
  }
}

} // namespace

void initUserInterface() {
  if (UI_HAS_STATUS_LEDS) {
    pinMode(HEARTBEAT_LED_PIN, OUTPUT);
    pinMode(SENSOR_LED_PIN, OUTPUT);
    pinMode(NETWORK_LED_PIN, OUTPUT);
    pinMode(POWER_LED_PIN, OUTPUT);

    writeUiLed(HEARTBEAT_LED_PIN, false);
    writeUiLed(SENSOR_LED_PIN, false);
    writeUiLed(NETWORK_LED_PIN, false);
    writeUiLed(POWER_LED_PIN, false);
  }

  if (UI_HAS_USER_BUTTON) {
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  }
}

void updateUserInterface() {
  if (!UI_HAS_STATUS_LEDS) {
    return;
  }

  unsigned long nowMs = millis();
  updateHeartbeatLed(nowMs);
  updateSensorLed(nowMs);
  updateNetworkLed(nowMs);
  updatePowerLed(nowMs);
}

void handleUserButton() {
  if (!UI_HAS_USER_BUTTON) {
    return;
  }

  static bool lastPhysicalPressed = false;
  static bool debouncedPressed = false;
  static unsigned long lastChangeMs = 0;
  bool physicalPressed = (digitalRead(USER_BUTTON_PIN) == LOW);
  unsigned long nowMs = millis();

  if (physicalPressed != lastPhysicalPressed) {
    lastPhysicalPressed = physicalPressed;
    lastChangeMs = nowMs;
  }

  if ((nowMs - lastChangeMs) < 30UL) return;

  if (physicalPressed != debouncedPressed) {
    debouncedPressed = physicalPressed;
    if (debouncedPressed) {
      wakeDisplayForTimeout();
    }
  }
}
