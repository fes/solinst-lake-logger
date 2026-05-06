namespace {

bool tryMountFs(mbed::FileSystem &fs, mbed::MBRBlockDevice &partition, const char* fsType, int partitionNumber) {
  int err = fs.mount(&partition);
  if (err == 0) {
    user_fs = &fs;
    userFsMounted = true;
    userFsType = fsType;
    userFsPartition = partitionNumber;
    return true;
  }
  return false;
}

void setConfigValue(char* target, size_t targetSize, const String& value) {
  String trimmed = value;
  trimmed.trim();

  if (trimmed.startsWith("\"") && trimmed.endsWith("\"") && trimmed.length() >= 2) {
    trimmed = trimmed.substring(1, trimmed.length() - 1);
  }

  trimmed.toCharArray(target, targetSize);
}

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
  if (userFsMounted && user_fs != nullptr) {
    return true;
  }

  if (qspiRoot == nullptr) {
    configLoadStatus = "QSPI block device not available";
    return false;
  }

  int err = qspiRoot->init();
  if (err != 0) {
    configLoadStatus = "QSPI init failed";
    return false;
  }

  // Try the current documented default first: partition 4 user data.
  if (tryMountFs(user_lfs, user_data_p4, "LittleFS", 4)) return true;
  if (tryMountFs(user_fatfs, user_data_p4, "FatFS", 4)) return true;

  // Fallback for older/custom partition layouts that may have used partition 3.
  if (tryMountFs(user_lfs, user_data_p3, "LittleFS", 3)) return true;
  if (tryMountFs(user_fatfs, user_data_p3, "FatFS", 3)) return true;

  configLoadStatus = "could not mount /user filesystem";
  return false;
}

bool loadRuntimeConfig() {
  bool mounted = mountUserFileSystem();
  if (!mounted) {
    buildPostPath();
    configSource = "defaults";
    Serial.println("Config: using built-in placeholders/defaults");
    Serial.print("Config mount status: ");
    Serial.println(configLoadStatus);
    return false;
  }

  FILE* fp = fopen("/user/config.ini", "r");
  if (fp == nullptr) {
    buildPostPath();
    configLoadStatus = "mounted /user but /user/config.ini not found";
    configSource = "defaults";
    Serial.println("Config: /user/config.ini not found, using built-in placeholders/defaults");
    return false;
  }

  int loadedKeys = 0;
  char lineBuf[384];

  while (fgets(lineBuf, sizeof(lineBuf), fp) != nullptr) {
    String line(lineBuf);
    line.trim();

    if (line.length() == 0) continue;
    if (line.startsWith("#") || line.startsWith(";")) continue;

    int eq = line.indexOf('=');
    if (eq <= 0) continue;

    String key = line.substring(0, eq);
    String value = line.substring(eq + 1);
    key.trim();
    value.trim();

    if (key.equalsIgnoreCase("WIFI_SSID")) {
      setConfigValue(WIFI_SSID, sizeof(WIFI_SSID), value);
      loadedKeys++;
    } else if (key.equalsIgnoreCase("WIFI_PASS")) {
      setConfigValue(WIFI_PASS, sizeof(WIFI_PASS), value);
      loadedKeys++;
    } else if (key.equalsIgnoreCase("DEVICE_ID")) {
      setConfigValue(DEVICE_ID, sizeof(DEVICE_ID), value);
      loadedKeys++;
    } else if (key.equalsIgnoreCase("SHARED_SECRET")) {
      setConfigValue(SHARED_SECRET, sizeof(SHARED_SECRET), value);
      loadedKeys++;
    } else if (key.equalsIgnoreCase("POST_PATH") || key.equalsIgnoreCase("POST_DEPLOYMENT_ID") || key.equalsIgnoreCase("DEPLOYMENT_ID")) {
      setConfigValue(POST_DEPLOYMENT_ID, sizeof(POST_DEPLOYMENT_ID), value);
      loadedKeys++;
    }
  }

  fclose(fp);

  buildPostPath();
  configLoadStatus = "loaded " + String(loadedKeys) + " keys from /user/config.ini";
  configSource = (loadedKeys > 0) ? "config.ini" : "defaults";
  return loadedKeys > 0;
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
}
