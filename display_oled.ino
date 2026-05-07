namespace {

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
  readPowerMonitors(snapshot);
  if (!snapshot.valid) {
    snapshot.timestampUtc = nowUtcString();
  }
  return snapshot;
}

} // namespace

bool initDisplay() {
  Wire.begin();
  display.setI2CAddress(DISPLAY_I2C_ADDRESS << 1);
  display.begin();
  display.setPowerSave(1);
  display.clearDisplay();
  displayPresent = true;
  displayAwake = false;
  displayWakeUntilMs = 0;
  Serial.println("SSD1309 display initialized in power-save mode");
  return true;
}

void wakeDisplayForTimeout() {
  if (!displayPresent) return;
  displayWakeUntilMs = millis() + (displayOnSeconds * 1000UL);
  if (!displayAwake) {
    display.setPowerSave(0);
    displayAwake = true;
  }
  updateDisplay();
}

void updateDisplay() {
  if (!displayPresent) return;

  unsigned long nowMs = millis();
  if (!displayAwake) return;

  if ((long)(displayWakeUntilMs - nowMs) <= 0) {
    sleepDisplay();
    return;
  }

  drawDisplayScreen(currentDisplaySnapshot());
}

void sleepDisplay() {
  if (!displayPresent || !displayAwake) return;
  display.clearBuffer();
  display.sendBuffer();
  display.setPowerSave(1);
  displayAwake = false;
}
