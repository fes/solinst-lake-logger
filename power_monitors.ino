namespace {

void configureIna228(Adafruit_INA228 &monitor) {
  monitor.setShunt(INA228_SHUNT_OHMS, INA228_MAX_CURRENT_AMPS);
  monitor.setAveragingCount(INA228_COUNT_16);
  monitor.setVoltageConversionTime(INA228_TIME_540_us);
  monitor.setCurrentConversionTime(INA228_TIME_540_us);
}

bool initOnePowerMonitor(Adafruit_INA228 &monitor, uint8_t address, const char *label, bool &present) {
  if (!monitor.begin(address, &Wire)) {
    Serial.print("INA228 ");
    Serial.print(label);
    Serial.print(" not found at 0x");
    Serial.println(address, HEX);
    present = false;
    return false;
  }

  configureIna228(monitor);

  Serial.print("INA228 ");
  Serial.print(label);
  Serial.print(" found at 0x");
  Serial.println(address, HEX);

  present = true;
  return true;
}

void readOnePowerMonitor(Adafruit_INA228 &monitor, bool present, PowerMonitorSnapshot &snapshot) {
  snapshot.present = present;
  snapshot.valid = false;
  snapshot.busVoltageV = NAN;
  snapshot.currentA = NAN;
  snapshot.powerW = NAN;

  if (!present) {
    return;
  }

  float busVoltageV = monitor.getBusVoltage_V();
  float currentA = monitor.getCurrent_mA() / 1000.0f;
  float powerW = monitor.getPower_mW() / 1000.0f;

  snapshot.busVoltageV = busVoltageV;
  snapshot.currentA = currentA;
  snapshot.powerW = powerW;
  snapshot.valid = isfinite(busVoltageV) && isfinite(currentA) && isfinite(powerW);
}

} // namespace

bool initPowerMonitors() {
  Wire.begin();
  delay(10);

  bool batteryOk = initOnePowerMonitor(
      batteryOutputMonitor,
      INA228_BATTERY_OUTPUT_ADDR,
      "battery_output",
      batteryOutputMonitorPresent);

  bool solarOk = initOnePowerMonitor(
      solarInputMonitor,
      INA228_SOLAR_INPUT_ADDR,
      "solar_input",
      solarInputMonitorPresent);

  if (batteryOk || solarOk) {
    powerMonitorInitStatus = "initialized";
  } else {
    powerMonitorInitStatus = "no INA228 devices found";
  }

  printPowerMonitorSummary();
  return batteryOk || solarOk;
}

void readPowerMonitors(ProbeReading &reading) {
  readOnePowerMonitor(batteryOutputMonitor, batteryOutputMonitorPresent, reading.batteryOutput);
  readOnePowerMonitor(solarInputMonitor, solarInputMonitorPresent, reading.solarInput);
}

void printPowerMonitorSummary() {
  Serial.println("INA228 monitor summary:");
  Serial.print("  battery_output address: 0x");
  Serial.println(INA228_BATTERY_OUTPUT_ADDR, HEX);
  Serial.print("  battery_output present: ");
  Serial.println(batteryOutputMonitorPresent ? "true" : "false");
  Serial.print("  solar_input address: 0x");
  Serial.println(INA228_SOLAR_INPUT_ADDR, HEX);
  Serial.print("  solar_input present: ");
  Serial.println(solarInputMonitorPresent ? "true" : "false");
  Serial.print("  init status: ");
  Serial.println(powerMonitorInitStatus);
}
