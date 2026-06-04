// Optional DFRobot SEN0657 7-in-1 RS-485/Modbus weather sensor support.
//
// The DFRobot reference says the sensor uses Modbus RTU function 0x03 for
// reads, factory default address 0x01, and factory default baud 4800 8N1.
// This logger is configured for a shared RS-485 bus with the Solinst 301, so
// the weather station should be configured to the same serial settings as the
// Solinst before enabling WEATHER_SENSOR_ENABLED.

constexpr uint16_t WEATHER_REG_WIND_SPEED = 0x01F4;
constexpr uint16_t WEATHER_REG_BLOCK_START = 0x01F4;
constexpr uint16_t WEATHER_REG_BLOCK_QTY = 14;

bool readWeatherHoldingRegistersOnce(uint8_t slaveId, uint16_t startReg, uint16_t quantity,
                                     uint16_t *values, const char **errorOut) {
  if (quantity == 0 || quantity > 16) {
    if (errorOut) *errorOut = "unsupported quantity";
    return false;
  }

  beginSolinstRs485();

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
  weather.rainfallMm = regs[13] / 10.0f;

  weather.valid = isfinite(weather.windSpeedMs) && isfinite(weather.windDirectionDeg) &&
                  isfinite(weather.relativeHumidityPct) && isfinite(weather.airTemperatureC) &&
                  isfinite(weather.barometricPressureHpa) && isfinite(weather.lightLux) &&
                  isfinite(weather.rainfallMm);
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
  readWeatherNow(reading.weather);
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
  body += "\"" + String(prefix) + "_rainfall_mm\":"; appendFloatOrNull(body, weather.rainfallMm, 2); body += ",";
  body += "\"" + String(prefix) + "_light_lux\":"; appendFloatOrNull(body, weather.lightLux, 1); body += ",";

  body += "\"" + String(prefix) + "_last_error\":\"" + jsonEscape(weather.lastError) + "\",";
}
