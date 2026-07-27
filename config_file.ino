namespace {

bool isPlaceholder(const char* actual, const char* placeholder) {
  return strcmp(actual, placeholder) == 0;
}

String maskedPreview(const char* value, const char* placeholder) {
  if (isPlaceholder(value, placeholder)) {
    return "placeholder";
  }

  String s(value);
  if (s.length() <= 4) {
    return String("set (") + s.length() + " chars)";
  }

  return s.substring(0, 2) + "..." + s.substring(s.length() - 2) +
         " (" + String(s.length()) + " chars)";
}

} // namespace

void buildPostPath() {
  snprintf(POST_PATH, sizeof(POST_PATH), "%s%s%s",
           POST_PATH_PREFIX,
           POST_DEPLOYMENT_ID,
           POST_PATH_SUFFIX);
}

bool loadRuntimeConfig() {
  buildPostPath();
  configSource = "compiled secrets";

#ifdef ALLOW_PLACEHOLDER_SECRETS
  configLoadStatus = "using placeholder/example secrets";
#else
  configLoadStatus = "using secrets_local.h";
#endif

  Serial.println("Config: initialized compile-time configuration");
  return true;
}

void printRuntimeConfigSummary() {
  Serial.println("Compile-time config summary:");
  Serial.print("  config source: ");
  Serial.println(configSource);
  Serial.print("  config status: ");
  Serial.println(configLoadStatus);

  Serial.println("Config self-test:");
  Serial.print("  WIFI_SSID: ");
  Serial.println(maskedPreview(WIFI_SSID, "YOUR_WIFI_SSID"));
  Serial.print("  WIFI_PASS: ");
  Serial.println(maskedPreview(WIFI_PASS, "YOUR_WIFI_PASSWORD"));
  Serial.print("  DEVICE_ID: ");
  Serial.println(DEVICE_ID);
  Serial.print("  SHARED_SECRET: ");
  Serial.println(maskedPreview(SHARED_SECRET, "PUT_A_LONG_RANDOM_SECRET_HERE"));
  Serial.print("  POST_DEPLOYMENT_ID: ");
  Serial.println(maskedPreview(POST_DEPLOYMENT_ID, "PUT_YOUR_DEPLOYMENT_ID_HERE"));
  Serial.print("  POST_PATH: ");
  Serial.println(POST_PATH);
  Serial.print("  DISPLAY_ON_SECONDS: ");
  Serial.println(displayOnSeconds);
  Serial.print("  WEATHER_SENSOR_ENABLED: ");
  Serial.println(WEATHER_SENSOR_ENABLED ? "true" : "false");
  Serial.print("  WEATHER_MODBUS_ID: ");
  Serial.println(WEATHER_MODBUS_ID);
  Serial.print("  WEATHER_BAUD: ");
  Serial.println(WEATHER_BAUD);
  Serial.print("  WEATHER_SAMPLE_INTERVAL_MS: ");
  Serial.println(WEATHER_SAMPLE_INTERVAL_MS);
}
