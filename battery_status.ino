float approximateBatteryChargePercent(const ProbeReading &reading) {
  if (!reading.batteryOutput.valid || !isfinite(reading.batteryOutput.busVoltageV)) {
    return NAN;
  }

  // Very rough LiFePO4 estimate from battery voltage only.
  // This is most meaningful near-rest and less accurate while charging/discharging.
  const float emptyV = 12.0f;
  const float fullV = 13.4f;
  float pct = ((reading.batteryOutput.busVoltageV - emptyV) / (fullV - emptyV)) * 100.0f;

  if (pct < 0.0f) pct = 0.0f;
  if (pct > 100.0f) pct = 100.0f;
  return pct;
}

bool solarChargingBatteryNow(const ProbeReading &reading) {
  return reading.solarInput.valid && isfinite(reading.solarInput.currentA) && reading.solarInput.currentA > 0.05f;
}

void appendPowerMonitorJson(String &body, const char *prefix, const PowerMonitorSnapshot &snapshot) {
  body += "\"" + String(prefix) + "_monitor_present\":" + String(snapshot.present ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_monitor_valid\":" + String(snapshot.valid ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_voltage_v\":";
  body += (snapshot.valid ? String(snapshot.busVoltageV, 3) : String("null"));
  body += ",";
  body += "\"" + String(prefix) + "_current_a\":";
  body += (snapshot.valid ? String(snapshot.currentA, 4) : String("null"));
  body += ",";
  body += "\"" + String(prefix) + "_power_w\":";
  body += (snapshot.valid ? String(snapshot.powerW, 4) : String("null"));
  body += ",";
}
