#include "config.h"

float approximateBatteryChargePercent(const ProbeReading &reading) {
  return logger_core::batteryChargePercent(
      reading.batteryOutput.valid, reading.batteryOutput.busVoltageV);
}

bool solarChargingBatteryNow(const ProbeReading &reading) {
  return logger_core::solarCharging(
      reading.solarInput.valid, reading.solarInput.currentA);
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
