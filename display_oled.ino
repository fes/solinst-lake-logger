namespace {

bool displayRefreshPending = false;
unsigned long displayLastWakeRequestMs = 0;
unsigned long displayLastRefreshMs = 0;
String displayLastWakeRequestUtc = "";
String displayLastRefreshUtc = "";
uint32_t displayRefreshCountLocal = 0;

bool i2cDevicePresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool initDisplayOnce(bool logFailures) {
  if (!i2cDevicePresent(DISPLAY_I2C_ADDRESS)) {
    displayPresent = false;
    displayAwake = false;
    displayWakeUntilMs = 0;
    displayRefreshPending = false;
    if (logFailures) {
      Serial.print("SSD1309 display not detected at 0x");
      Serial.println(DISPLAY_I2C_ADDRESS, HEX);
    }
    return false;
  }

  display.setI2CAddress(DISPLAY_I2C_ADDRESS << 1);
  display.begin();
  display.setPowerSave(1);
  display.clearBuffer();
  display.sendBuffer();
  displayPresent = true;
  displayAwake = false;
  displayWakeUntilMs = 0;
  displayRefreshPending = false;
  Serial.println("SSD1309 display initialized in power-save mode");
  return true;
}

void reinitializeDisplayForWake() {
  Serial.println("UI: performing OLED re-init on wake");
  display.setI2CAddress(DISPLAY_I2C_ADDRESS << 1);
  display.begin();
  display.clearBuffer();
  display.sendBuffer();
  display.setPowerSave(0);
  displayAwake = true;
}

void drawDisplayScreen(const ProbeReading &snapshot) {
  float batteryPct = approximateBatteryChargePercent(snapshot);
  bool charging = solarChargingBatteryNow(snapshot);

  display.clearBuffer();
  display.setFont(u8g2_font_6x12_tf);
  display.drawStr(0, 10, DEVICE_ID);

  char line[64];
  if (snapshot.valid) {
    snprintf(line, sizeof(line), "Level: %.3f m", snapshot.level);
  } else {
    snprintf(line, sizeof(line), "Level: --");
  }
  display.drawStr(0, 22, line);

  if (snapshot.valid) {
    snprintf(line, sizeof(line), "Temp:  %.2f C", snapshot.temperature);
  } else {
    snprintf(line, sizeof(line), "Temp:  --");
  }
  display.drawStr(0, 34, line);

  if (snapshot.batteryOutput.valid) {
    snprintf(line, sizeof(line), "Batt:  %.2fV %.0f%%", snapshot.batteryOutput.busVoltageV,
             isfinite(batteryPct) ? batteryPct : 0.0f);
  } else {
    snprintf(line, sizeof(line), "Batt:  --");
  }
  display.drawStr(0, 46, line);

  if (snapshot.solarInput.valid) {
    snprintf(line, sizeof(line), "Solar: %.2fA %s", snapshot.solarInput.currentA,
             charging ? "CHG" : "IDLE");
  } else {
    snprintf(line, sizeof(line), "Solar: --");
  }
  display.drawStr(0, 58, line);

  display.sendBuffer();
}

ProbeReading currentDisplaySnapshot() {
  ProbeReading snapshot = lastProbeReading;
  if (!snapshot.valid) {
    snapshot.timestampUtc = nowUtcString();
  }
  return snapshot;
}

} // namespace

bool initDisplay() {
  Wire.begin();
  delay(20);

  for (int attempt = 1; attempt <= 5; attempt++) {
    if (initDisplayOnce(attempt == 5)) {
      if (attempt > 1) {
        Serial.print("SSD1309 display detected after retry ");
        Serial.println(attempt);
      }
      return true;
    }
    delay(100);
  }

  return false;
}

void wakeDisplayForTimeout() {
  displayLastWakeRequestMs = millis();
  displayLastWakeRequestUtc = nowUtcString();

  if (!displayPresent) {
    Serial.println("UI: display not present, retrying display init");
    if (!initDisplay()) {
      Serial.println("UI: display init retry failed");
      return;
    }
    Serial.println("UI: display re-detected on wake request");
  }

  displayWakeUntilMs = millis() + (displayOnSeconds * 1000UL);
  displayRefreshPending = true;

  if (!displayAwake) {
    reinitializeDisplayForWake();
  }
}

void updateDisplay() {
  static unsigned long lastRefreshMs = 0;

  if (!displayPresent) return;
  if (!displayAwake) return;

  unsigned long nowMs = millis();
  if ((long)(displayWakeUntilMs - nowMs) <= 0) {
    sleepDisplay();
    return;
  }

  if (!displayRefreshPending && (nowMs - lastRefreshMs) < DISPLAY_REFRESH_INTERVAL_MS) {
    return;
  }

  lastRefreshMs = nowMs;
  displayRefreshPending = false;
  displayLastRefreshMs = nowMs;
  displayLastRefreshUtc = nowUtcString();
  displayRefreshCountLocal++;
  drawDisplayScreen(currentDisplaySnapshot());
}

void sleepDisplay() {
  if (!displayPresent || !displayAwake) return;
  display.clearBuffer();
  display.sendBuffer();
  display.setPowerSave(1);
  displayAwake = false;
  displayRefreshPending = false;
}

String lastDisplayWakeRequestUtc() {
  return displayLastWakeRequestUtc;
}

String lastDisplayRefreshUtc() {
  return displayLastRefreshUtc;
}

String lastDisplayWakeRequestAge() {
  return millisAgeString(displayLastWakeRequestMs);
}

String lastDisplayRefreshAge() {
  return millisAgeString(displayLastRefreshMs);
}

uint32_t displayRefreshCount() {
  return displayRefreshCountLocal;
}
