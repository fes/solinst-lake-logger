// Optional DFRobot SEN0657 7-in-1 RS-485/Modbus weather sensor support.
//
// The DFRobot reference says the sensor uses Modbus RTU function 0x03 for
// reads, factory default address 0x01, and factory default baud 4800 8N1.
// This logger shares its RS-485 bus with the Solinst 301. Configure the weather
// station to the same baud rate and a unique address; the Opta switches between
// the weather station's 8N1 and the Solinst's 8E1 framing for each request.

constexpr uint16_t WEATHER_REG_WIND_SPEED = 0x01F4;
constexpr uint16_t WEATHER_REG_BLOCK_START = 0x01F4;
constexpr uint16_t WEATHER_REG_BLOCK_QTY = 14;

bool readWeatherHoldingRegistersOnce(uint8_t slaveId, uint16_t startReg, uint16_t quantity,
                                     uint16_t *values, const char **errorOut) {
  if (quantity == 0 || quantity > 16) {
    if (errorOut) *errorOut = "unsupported quantity";
    return false;
  }

  beginWeatherRs485();

  uint8_t request[8];
  request[0] = slaveId;
  request[1] = 0x03;
  request[2] = highByte(startReg);
  request[3] = lowByte(startReg);
  request[4] = highByte(quantity);
  request[5] = lowByte(quantity);
  uint16_t crc = modbusCrc16(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  clearRs485ReceiveBuffer();
  rs485WriteBytes(request, sizeof(request));

  uint8_t responseBuffer[96];
  const uint8_t expectedByteCount = quantity * 2;
  int responseOffset = -1;

  size_t responseLength = readRawResponseUntilCandidate(
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
    return false;
  }

  const uint8_t *response = responseBuffer + responseOffset;
  if (responseOffset > 0) {
    Serial.print("Weather Modbus RX ignored echo/noise prefix of ");
    Serial.print(responseOffset);
    Serial.println(" byte(s)");
  }

  for (uint16_t i = 0; i < quantity; i++) {
    size_t offset = 3 + (i * 2);
    values[i] = ((uint16_t)response[offset] << 8) | response[offset + 1];
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

void addWeatherValue(float value, float &sum, float &minimum, float &maximum) {
  sum += value;
  if (!isfinite(minimum) || value < minimum) minimum = value;
  if (!isfinite(maximum) || value > maximum) maximum = value;
}

void addWeatherSample(const WeatherReading &weather) {
  if (!weather.valid) return;

  if (weatherSummary.sampleCount == 0) {
    weatherSummary.startUtc = weather.readUtc;
    weatherSummary.rainfallFirstMm = weather.rainfallAccumulatedMm;
    weatherSummary.rainfallLastMm = weather.rainfallAccumulatedMm;
  } else if (weather.rainfallAccumulatedMm < weatherSummary.rainfallLastMm) {
    weatherSummary.rainfallCounterReset = true;
  }

  weatherSummary.sampleCount++;
  weatherSummary.endUtc = weather.readUtc;
  weatherSummary.rainfallLastMm = weather.rainfallAccumulatedMm;
  addWeatherValue(weather.airTemperatureC, weatherSummary.airTemperatureSum, weatherSummary.airTemperatureMin, weatherSummary.airTemperatureMax);
  addWeatherValue(weather.relativeHumidityPct, weatherSummary.relativeHumiditySum, weatherSummary.relativeHumidityMin, weatherSummary.relativeHumidityMax);
  addWeatherValue(weather.barometricPressureHpa, weatherSummary.barometricPressureSum, weatherSummary.barometricPressureMin, weatherSummary.barometricPressureMax);
  addWeatherValue(weather.windSpeedMs, weatherSummary.windSpeedSum, weatherSummary.windSpeedMin, weatherSummary.windSpeedMax);
  addWeatherValue(weather.lightLux, weatherSummary.lightSum, weatherSummary.lightMin, weatherSummary.lightMax);

  float radians = weather.windDirectionDeg * PI / 180.0f;
  weatherSummary.windDirectionSinSum += sin(radians);
  weatherSummary.windDirectionCosSum += cos(radians);
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

  uint16_t regs[WEATHER_REG_BLOCK_QTY];
  if (!readWeatherHoldingRegistersWithRetry(WEATHER_MODBUS_ID, WEATHER_REG_BLOCK_START, WEATHER_REG_BLOCK_QTY, regs)) {
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
  weather.windSpeedMs = regs[0] / 10.0f;
  // regs[1] is not documented in the DFRobot table.
  weather.windDirectionDeg = regs[3];
  weather.relativeHumidityPct = regs[4] / 10.0f;
  weather.airTemperatureC = ((int16_t)regs[5]) / 10.0f;
  weather.barometricPressureHpa = regs[9];

  uint32_t light32 = ((uint32_t)regs[10] << 16) | regs[11];
  weather.lightLux = light32 > 0 ? (float)light32 : (float)regs[12];
  weather.rainfallAccumulatedMm = regs[13] / 10.0f;

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
  pollWeatherIfDue(true);
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
    float direction = atan2(summary.windDirectionSinSum, summary.windDirectionCosSum) * 180.0f / PI;
    if (direction < 0) direction += 360.0f;
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
  float rainfallInterval = summary.rainfallCounterReset || !isfinite(summary.rainfallFirstMm) ? NAN :
                           summary.rainfallLastMm - summary.rainfallFirstMm;
  body += "\"" + String(prefix) + "_rainfall_interval_mm\":"; appendFloatOrNull(body, rainfallInterval, 2); body += ",";
  body += "\"" + String(prefix) + "_rainfall_counter_reset\":" + String(summary.rainfallCounterReset ? "true" : "false") + ",";
}
