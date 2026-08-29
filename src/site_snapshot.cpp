#include "config.h"

logger_core::SiteSnapshot currentSiteSnapshot() {
  logger_core::SiteSnapshot snapshot;
  snapshot.capturedMs = millis();
  snapshot.readingRevision = siteReadingRevision;
  snapshot.clockValid = isClockValid();
  snapshot.wifiConnected = WiFi.status() == WL_CONNECTED;
  snapshot.wifiRssiDbm = snapshot.wifiConnected ? WiFi.RSSI() : 0;
  if (snapshot.wifiConnected) {
    const IPAddress ip = WiFi.localIP();
    for (size_t i = 0; i < 4; ++i) snapshot.ipAddress[i] = ip[i];
  }
  snapshot.sensorFound = detectedSensorId != 0;
  snapshot.sensorModbusId = detectedSensorId;
  snapshot.sensorSerialNumber = detectedIdentity.serialNumber;
  snapshot.waterValid = lastProbeReading.valid;
  snapshot.waterLevelM = lastProbeReading.level;
  snapshot.waterTemperatureC = lastProbeReading.temperature;
  snapshot.probeAgeValid = lastSuccessfulProbeReadMs != 0;
  snapshot.probeAgeSeconds = snapshot.probeAgeValid
                                 ? (millis() - lastSuccessfulProbeReadMs) / 1000UL
                                 : 0;
  snapshot.weatherEnabled = WEATHER_SENSOR_ENABLED;
  snapshot.weatherPresent = lastWeatherReading.present;
  snapshot.weatherValid = lastWeatherReading.valid;
  snapshot.airTemperatureC = lastWeatherReading.airTemperatureC;
  snapshot.relativeHumidityPct = lastWeatherReading.relativeHumidityPct;
  snapshot.barometricPressureHpa =
      lastWeatherReading.barometricPressureHpa;
  snapshot.windSpeedMs = lastWeatherReading.windSpeedMs;
  snapshot.windDirectionDeg = lastWeatherReading.windDirectionDeg;
  snapshot.rainfallIntervalMm =
      logger_core::rainfallIntervalMm(weatherSummary);
  snapshot.batteryValid = latestPowerSnapshot.batteryOutput.valid;
  snapshot.batteryVoltageV =
      latestPowerSnapshot.batteryOutput.busVoltageV;
  snapshot.batteryChargePct = logger_core::batteryChargePercent(
      snapshot.batteryValid, snapshot.batteryVoltageV);
  snapshot.batteryExtrema24hValid = batteryVoltage24hExtrema(
      snapshot.batteryVoltageMin24hV, snapshot.batteryVoltageMax24hV);
  snapshot.solarValid = latestPowerSnapshot.solarInput.valid;
  snapshot.solarVoltageV = latestPowerSnapshot.solarInput.busVoltageV;
  snapshot.solarCurrentA = latestPowerSnapshot.solarInput.currentA;
  snapshot.solarPowerW = latestPowerSnapshot.solarInput.powerW;
  snapshot.solarCharging = logger_core::solarCharging(
      snapshot.solarValid, snapshot.solarCurrentA);
  snapshot.backlogCount = backlogStorage.count();
  snapshot.consecutiveUploadFailures = consecutiveUploadFailures;
  snapshot.uploadAgeValid = lastSuccessfulUploadMs != 0;
  snapshot.uploadAgeSeconds =
      snapshot.uploadAgeValid
          ? (millis() - lastSuccessfulUploadMs) / 1000UL
          : 0;
  snapshot.health = logger_core::classifySiteHealth(snapshot);
  return snapshot;
}
