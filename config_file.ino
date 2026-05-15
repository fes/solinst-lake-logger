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

bool mountUserFileSystem() {
  configLoadStatus = "runtime config disabled";
  return false;
}

bool loadRuntimeConfig() {
  buildPostPath();
  configSource = "compiled secrets";
  configLoadStatus = "runtime config disabled";
  Serial.println("Config: using compile-time secrets header");
  return false;
}

void printRuntimeConfigSummary() {
  Serial.println("Runtime config summary:");
  Serial.print("  config source: ");
  Serial.println(configSource);
  Serial.print("  user fs mounted: ");
  Serial.println(userFsMounted ? "true" : "false");
  Serial.print("  user fs type: ");
  Serial.println(userFsType);
  Serial.print("  user fs partition: ");
  Serial.println(userFsPartition);
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
}
