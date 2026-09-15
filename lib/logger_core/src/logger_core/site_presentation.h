#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logger_core {

enum class SiteHealth : uint8_t {
  HEALTHY,
  DEGRADED,
  CRITICAL
};

struct SiteSnapshot {
  uint32_t capturedMs = 0;
  uint32_t readingRevision = 0;
  uint32_t weatherRevision = 0;
  bool clockValid = false;
  bool wifiConnected = false;
  int32_t wifiRssiDbm = 0;
  uint8_t ipAddress[4] = {};
  bool sensorFound = false;
  uint8_t sensorModbusId = 0;
  uint32_t sensorSerialNumber = 0;
  bool waterValid = false;
  float waterLevelM = 0;
  float waterTemperatureC = 0;
  bool probeAgeValid = false;
  uint32_t probeAgeSeconds = 0;
  bool weatherEnabled = false;
  bool weatherPresent = false;
  bool weatherValid = false;
  float airTemperatureC = 0;
  float relativeHumidityPct = 0;
  float barometricPressureHpa = 0;
  float windSpeedMs = 0;
  float windDirectionDeg = 0;
  float rainfallIntervalMm = 0;
  bool batteryValid = false;
  float batteryVoltageV = 0;
  float batteryChargePct = 0;
  bool batteryExtrema24hValid = false;
  float batteryVoltageMin24hV = 0;
  float batteryVoltageMax24hV = 0;
  bool solarValid = false;
  float solarVoltageV = 0;
  float solarCurrentA = 0;
  float solarPowerW = 0;
  bool solarCharging = false;
  size_t backlogCount = 0;
  uint32_t consecutiveUploadFailures = 0;
  bool uploadAgeValid = false;
  uint32_t uploadAgeSeconds = 0;
  SiteHealth health = SiteHealth::CRITICAL;
};

SiteHealth classifySiteHealth(const SiteSnapshot& snapshot);
const char* siteHealthName(SiteHealth health);

}  // namespace logger_core
