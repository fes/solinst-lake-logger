// Optional Modbus weather sensor framework.
// Placeholder until the DFRobot 7-in-1 weather sensor register map is available.

void initWeatherSensor() {
  weatherSensorPresent = false;
  weatherSensorValid = false;

  if (!WEATHER_SENSOR_ENABLED) {
    weatherSensorInitStatus = "disabled";
    lastWeatherError = "weather sensor disabled";
    Serial.println("Weather sensor disabled");
    return;
  }

  weatherSensorInitStatus = "enabled; register map not implemented";
  lastWeatherError = "weather register map not implemented";
  Serial.print("Weather sensor framework enabled at Modbus ID ");
  Serial.println(WEATHER_MODBUS_ID);
}

bool readWeatherNow(WeatherReading &weather) {
  weather = WeatherReading();
  weather.enabled = WEATHER_SENSOR_ENABLED;
  weather.modbusId = WEATHER_MODBUS_ID;
  weather.readUtc = nowUtcString();

  if (!WEATHER_SENSOR_ENABLED) {
    weather.lastError = "disabled";
    lastWeatherError = weather.lastError;
    return false;
  }

  weather.present = weatherSensorPresent;
  weather.valid = false;
  weather.lastError = "weather register map not implemented";
  weatherSensorValid = false;
  lastWeatherAttemptMs = millis();
  lastWeatherError = weather.lastError;
  lastWeatherErrorUtc = weather.readUtc;
  return false;
}

void readWeatherForReading(ProbeReading &reading) {
  readWeatherNow(reading.weather);
}

void appendWeatherJson(String &body, const char *prefix, const WeatherReading &weather) {
  body += "\"" + String(prefix) + "_enabled\":" + String(weather.enabled ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_present\":" + String(weather.present ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_valid\":" + String(weather.valid ? "true" : "false") + ",";
  body += "\"" + String(prefix) + "_modbus_id\":" + String(weather.modbusId) + ",";
  body += "\"" + String(prefix) + "_read_utc\":\"" + jsonEscape(weather.readUtc) + "\",";
  body += "\"" + String(prefix) + "_last_error\":\"" + jsonEscape(weather.lastError) + "\",";
}
