#include "config.h"

// Optional DFRobot SEN0657 7-in-1 RS-485/Modbus weather sensor support.
//
// The DFRobot reference says the sensor uses Modbus RTU function 0x03 for
// reads, factory default address 0x01, and factory default baud 4800 8N1.
// This logger shares its RS-485 bus with the Solinst 301. Configure the weather
// station to the same baud rate and a unique address; the Opta switches between
// the weather station's 8N1 and the Solinst's 8E1 framing for each request.

constexpr uint16_t WEATHER_REG_WIND_SPEED = 0x01F4;
constexpr uint16_t WEATHER_REG_WIND_DIRECTION = 0x01F7;
constexpr uint16_t WEATHER_REG_HUMIDITY = 0x01F8;
constexpr uint16_t WEATHER_REG_PRESSURE = 0x01FD;
constexpr uint16_t WEATHER_REG_LIGHT_HIGH = 0x01FE;
constexpr uint16_t WEATHER_REG_RAINFALL = 0x0201;

bool readWeatherHoldingRegistersOnce(uint8_t slaveId, uint16_t startReg, uint16_t quantity,
                                     uint16_t *values, const char **errorOut) {
  if (quantity == 0 || quantity > 16) {
    if (errorOut) *errorOut = "unsupported quantity";
    return false;
  }

  beginWeatherRs485();
  Rs485Channel &channel = weatherRs485Channel();

  uint8_t request[logger_core::MODBUS_READ_REQUEST_BYTES];
  if (logger_core::buildModbusReadRequest(
          slaveId, 0x03, startReg, quantity, request, sizeof(request)) == 0) {
    if (errorOut) *errorOut = "invalid Modbus read request";
    return false;
  }

  clearRs485ReceiveBuffer(channel);
  if (!rs485WriteBytes(channel, request, sizeof(request))) {
    if (errorOut) *errorOut = "failed to write weather Modbus request";
    return false;
  }

  uint8_t responseBuffer[96];
  const uint8_t expectedByteCount = quantity * 2;
  int responseOffset = -1;

  size_t responseLength = readRawResponseUntilCandidate(
    channel,
    responseBuffer,
    sizeof(responseBuffer),
    WLTS_RESPONSE_TIMEOUT_MS,
    slaveId,
    0x03,
    expectedByteCount,
    responseOffset
  );

  if (responseOffset < 0) {
    if (errorOut) {
      *errorOut = validateModbusResponse(responseBuffer, responseLength, slaveId, 0x03, expectedByteCount);
      if (*errorOut == nullptr) *errorOut = "no valid weather response candidate found in RX buffer";
    }

    Serial.print("Weather Modbus read failed: slave=");
    Serial.print(slaveId);
    Serial.print(" reg=0x");
    Serial.print(startReg, HEX);
    Serial.print(" qty=");
    Serial.print(quantity);
    Serial.print(" err=");
    Serial.println(errorOut && *errorOut ? *errorOut : "unknown");
    printModbusBytes("Weather RX raw", responseBuffer, responseLength);
    return false;
  }

  if (responseOffset > 0) {
    Serial.print("Weather Modbus RX ignored echo/noise prefix of ");
    Serial.print(responseOffset);
    Serial.println(" byte(s)");
  }

  size_t decodedOffset = 0;
  if (logger_core::decodeModbusReadRegisters(
          responseBuffer, responseLength, slaveId, 0x03, quantity, values,
          quantity, &decodedOffset) != logger_core::ModbusDecodeResult::OK ||
      decodedOffset != static_cast<size_t>(responseOffset)) {
    if (errorOut) *errorOut = "failed to decode validated weather response";
    return false;
  }

  if (errorOut) *errorOut = nullptr;
  return true;
}

bool readWeatherHoldingRegistersWithRetry(uint8_t slaveId, uint16_t startReg, uint16_t quantity, uint16_t *values) {
  unsigned long backoff = INITIAL_BACKOFF_MS;
  const char *lastError = nullptr;

  for (int attempt = 1; attempt <= READ_RETRIES; attempt++) {
    if (readWeatherHoldingRegistersOnce(slaveId, startReg, quantity, values, &lastError)) {
      return true;
    }

    Serial.print("Weather read failed: slave=");
    Serial.print(slaveId);
    Serial.print(" reg=0x");
    Serial.print(startReg, HEX);
    Serial.print(" qty=");
    Serial.print(quantity);
    Serial.print(" attempt ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(READ_RETRIES);
    Serial.print(" err=");
    Serial.println(lastError ? lastError : "unknown");

    if (attempt < READ_RETRIES) {
      delay(backoff);
      backoff = min(backoff * 2, MAX_BACKOFF_MS);
    }
  }

  lastWeatherError = lastError ? String(lastError) : String("weather read failed");
  return false;
}

void initWeatherSensor() {
  weatherSensorPresent = false;
  weatherSensorValid = false;
  lastWeatherReading = WeatherReading();
  weatherSummary = WeatherSummary();

  if (!WEATHER_SENSOR_ENABLED) {
    weatherSensorInitStatus = "disabled";
    lastWeatherError = "weather sensor disabled";
    Serial.println("Weather sensor disabled");
    return;
  }

  weatherSensorInitStatus = "enabled";
  lastWeatherError = "";
  Serial.print("Weather sensor enabled at Modbus ID ");
  Serial.println(WEATHER_MODBUS_ID);
}

void addWeatherSample(const WeatherReading &weather) {
  if (!weather.valid) return;

  if (weatherSummary.sampleCount == 0) {
    weatherSummary.startUtc = weather.readUtc;
  }

  weatherSummary.endUtc = weather.readUtc;
  logger_core::WeatherSample sample;
  sample.airTemperatureC = weather.airTemperatureC;
  sample.relativeHumidityPct = weather.relativeHumidityPct;
  sample.barometricPressureHpa = weather.barometricPressureHpa;
  sample.windSpeedMs = weather.windSpeedMs;
  sample.windDirectionDeg = weather.windDirectionDeg;
  sample.rainfallAccumulatedMm = weather.rainfallAccumulatedMm;
  sample.lightLux = weather.lightLux;
  logger_core::addWeatherSample(weatherSummary, sample);
}

bool sampleWeatherNow() {
  WeatherReading sample;
  bool readOk = readWeatherNow(sample);
  lastWeatherReading = sample;
  if (readOk) addWeatherSample(sample);
  return readOk;
}

void pollWeatherIfDue(bool force) {
  if (!WEATHER_SENSOR_ENABLED) return;
  if (!force && lastWeatherAttemptMs != 0 &&
      millis() - lastWeatherAttemptMs < WEATHER_SAMPLE_INTERVAL_MS) return;
  sampleWeatherNow();
}

void resetWeatherSummaryForNextInterval() {
  weatherSummary = WeatherSummary();
  if (lastWeatherReading.valid) addWeatherSample(lastWeatherReading);
}

bool readWeatherNow(WeatherReading &weather) {
  if (weatherSensorInitStatus == "not initialized") {
    initWeatherSensor();
  }

  weather = WeatherReading();
  weather.enabled = WEATHER_SENSOR_ENABLED;
  weather.modbusId = WEATHER_MODBUS_ID;
  weather.readUtc = nowUtcString();
  lastWeatherAttemptMs = millis();

  if (!WEATHER_SENSOR_ENABLED) {
    weather.lastError = "disabled";
    lastWeatherError = weather.lastError;
    return false;
  }

  uint16_t windSpeed[1];
  uint16_t windDirection[1];
  uint16_t humidityTemperature[2];
  uint16_t pressure[1];
  uint16_t light[2];
  uint16_t rainfall[1];

  // The SEN0657 returns a Modbus exception for a block spanning its unsupported
  // noise/particulate registers, so read only the documented 7-in-1 registers.
  bool readOk =
      readWeatherHoldingRegistersWithRetry(WEATHER_MODBUS_ID, WEATHER_REG_WIND_SPEED, 1, windSpeed) &&
      readWeatherHoldingRegistersWithRetry(WEATHER_MODBUS_ID, WEATHER_REG_WIND_DIRECTION, 1, windDirection) &&
      readWeatherHoldingRegistersWithRetry(WEATHER_MODBUS_ID, WEATHER_REG_HUMIDITY, 2, humidityTemperature) &&
      readWeatherHoldingRegistersWithRetry(WEATHER_MODBUS_ID, WEATHER_REG_PRESSURE, 1, pressure) &&
      readWeatherHoldingRegistersWithRetry(WEATHER_MODBUS_ID, WEATHER_REG_LIGHT_HIGH, 2, light) &&
      readWeatherHoldingRegistersWithRetry(WEATHER_MODBUS_ID, WEATHER_REG_RAINFALL, 1, rainfall);

  if (!readOk) {
    weather.present = false;
    weather.valid = false;
    weather.lastError = lastWeatherError.length() > 0 ? lastWeatherError : String("weather read failed");
    weatherSensorPresent = false;
    weatherSensorValid = false;
    lastWeatherError = weather.lastError;
    lastWeatherErrorUtc = nowUtcString();
    return false;
  }

  weather.present = true;
  weather.windSpeedMs = windSpeed[0] / 10.0f;
  weather.windDirectionDeg = windDirection[0];
  weather.relativeHumidityPct = humidityTemperature[0] / 10.0f;
  weather.airTemperatureC = ((int16_t)humidityTemperature[1]) / 10.0f;
  weather.barometricPressureHpa = pressure[0];
  weather.lightLux = (float)(((uint32_t)light[0] << 16) | light[1]);
  weather.rainfallAccumulatedMm = rainfall[0] / 10.0f;

  weather.valid = isfinite(weather.windSpeedMs) && isfinite(weather.windDirectionDeg) &&
                  isfinite(weather.relativeHumidityPct) && isfinite(weather.airTemperatureC) &&
                  isfinite(weather.barometricPressureHpa) && isfinite(weather.lightLux) &&
                  isfinite(weather.rainfallAccumulatedMm);
  weather.lastError = weather.valid ? String("") : String("weather values invalid");

  weatherSensorPresent = weather.present;
  weatherSensorValid = weather.valid;
  if (weather.valid) {
    lastSuccessfulWeatherReadMs = millis();
    lastSuccessfulWeatherReadUtc = weather.readUtc;
    lastWeatherError = "";
    lastWeatherErrorUtc = "";
  } else {
    lastWeatherError = weather.lastError;
    lastWeatherErrorUtc = nowUtcString();
  }

  return weather.valid;
}

void readWeatherForReading(ProbeReading &reading) {
  // Reuse the boundary sample if the manager just took one; otherwise this
  // performs the normal due check. Forcing here double-counts every interval
  // boundary in the weather summary.
  pollWeatherIfDue(false);
  reading.weather = lastWeatherReading;
  reading.weather.summary = weatherSummary;
}

void appendFloatOrNull(String &body, float value, uint8_t decimals) {
  body += isfinite(value) ? String(value, decimals) : String("null");
}

void appendWeatherJson(String &body, const char *prefix, const WeatherReading &weather) {
  body += "\"" + String(prefix) + "_enabled\":" + String(weather.enabled ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_present\":" + String(weather.present ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_valid\":" + String(weather.valid ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_modbus_id\":" + String(weather.modbusId) + ",";
  body += "\"" + String(prefix) + "_read_utc\":\"" + jsonEscape(weather.readUtc) + "\",";

  body += "\"" + String(prefix) + "_air_temperature_c\":"; appendFloatOrNull(body, weather.airTemperatureC, 2); body += ",";
  body += "\"" + String(prefix) + "_relative_humidity_pct\":"; appendFloatOrNull(body, weather.relativeHumidityPct, 2); body += ",";
  body += "\"" + String(prefix) + "_barometric_pressure_hpa\":"; appendFloatOrNull(body, weather.barometricPressureHpa, 2); body += ",";
  body += "\"" + String(prefix) + "_wind_speed_m_s\":"; appendFloatOrNull(body, weather.windSpeedMs, 2); body += ",";
  body += "\"" + String(prefix) + "_wind_direction_deg\":"; appendFloatOrNull(body, weather.windDirectionDeg, 1); body += ",";
  body += "\"" + String(prefix) + "_rainfall_mm\":"; appendFloatOrNull(body, weather.rainfallAccumulatedMm, 2); body += ",";
  body += "\"" + String(prefix) + "_light_lux\":"; appendFloatOrNull(body, weather.lightLux, 1); body += ",";

  body += "\"" + String(prefix) + "_last_error\":\"" + jsonEscape(weather.lastError) + "\",";
  const WeatherSummary &summary = weather.summary;
  body += "\"" + String(prefix) + "_summary_sample_count\":" + String(summary.sampleCount) + ",";
  body += "\"" + String(prefix) + "_summary_start_utc\":\"" + jsonEscape(summary.startUtc) + "\",";
  body += "\"" + String(prefix) + "_summary_end_utc\":\"" + jsonEscape(summary.endUtc) + "\",";

  if (summary.sampleCount > 0) {
    float count = summary.sampleCount;
    float direction = logger_core::averageWindDirectionDegrees(summary);
    body += "\"" + String(prefix) + "_air_temperature_c_avg\":"; appendFloatOrNull(body, summary.airTemperatureSum / count, 2); body += ",";
    body += "\"" + String(prefix) + "_air_temperature_c_min\":"; appendFloatOrNull(body, summary.airTemperatureMin, 2); body += ",";
    body += "\"" + String(prefix) + "_air_temperature_c_max\":"; appendFloatOrNull(body, summary.airTemperatureMax, 2); body += ",";
    body += "\"" + String(prefix) + "_relative_humidity_pct_avg\":"; appendFloatOrNull(body, summary.relativeHumiditySum / count, 2); body += ",";
    body += "\"" + String(prefix) + "_relative_humidity_pct_min\":"; appendFloatOrNull(body, summary.relativeHumidityMin, 2); body += ",";
    body += "\"" + String(prefix) + "_relative_humidity_pct_max\":"; appendFloatOrNull(body, summary.relativeHumidityMax, 2); body += ",";
    body += "\"" + String(prefix) + "_barometric_pressure_hpa_avg\":"; appendFloatOrNull(body, summary.barometricPressureSum / count, 2); body += ",";
    body += "\"" + String(prefix) + "_barometric_pressure_hpa_min\":"; appendFloatOrNull(body, summary.barometricPressureMin, 2); body += ",";
    body += "\"" + String(prefix) + "_barometric_pressure_hpa_max\":"; appendFloatOrNull(body, summary.barometricPressureMax, 2); body += ",";
    body += "\"" + String(prefix) + "_wind_speed_m_s_avg\":"; appendFloatOrNull(body, summary.windSpeedSum / count, 2); body += ",";
    body += "\"" + String(prefix) + "_wind_speed_m_s_min\":"; appendFloatOrNull(body, summary.windSpeedMin, 2); body += ",";
    body += "\"" + String(prefix) + "_wind_speed_m_s_max\":"; appendFloatOrNull(body, summary.windSpeedMax, 2); body += ",";
    body += "\"" + String(prefix) + "_wind_direction_deg_avg\":"; appendFloatOrNull(body, direction, 1); body += ",";
    body += "\"" + String(prefix) + "_light_lux_avg\":"; appendFloatOrNull(body, summary.lightSum / count, 1); body += ",";
    body += "\"" + String(prefix) + "_light_lux_min\":"; appendFloatOrNull(body, summary.lightMin, 1); body += ",";
    body += "\"" + String(prefix) + "_light_lux_max\":"; appendFloatOrNull(body, summary.lightMax, 1); body += ",";
  } else {
    const char *summaryFields = "air_temperature_c_avg,air_temperature_c_min,air_temperature_c_max,relative_humidity_pct_avg,relative_humidity_pct_min,relative_humidity_pct_max,barometric_pressure_hpa_avg,barometric_pressure_hpa_min,barometric_pressure_hpa_max,wind_speed_m_s_avg,wind_speed_m_s_min,wind_speed_m_s_max,wind_direction_deg_avg,light_lux_avg,light_lux_min,light_lux_max";
    String fields(summaryFields);
    while (fields.length() > 0) {
      int comma = fields.indexOf(',');
      String field = comma < 0 ? fields : fields.substring(0, comma);
      body += "\"" + String(prefix) + "_" + field + "\":null,";
      if (comma < 0) break;
      fields = fields.substring(comma + 1);
    }
  }
  float rainfallInterval = logger_core::rainfallIntervalMm(summary);
  body += "\"" + String(prefix) + "_rainfall_interval_mm\":"; appendFloatOrNull(body, rainfallInterval, 2); body += ",";
  body += "\"" + String(prefix) + "_rainfall_counter_reset\":" + String(summary.rainfallCounterReset ? "true" : "false") + ",";
}
