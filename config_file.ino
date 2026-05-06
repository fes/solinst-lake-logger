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

} // namespace

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
    Serial.println("Config: using built-in placeholders/defaults");
    Serial.print("Config mount status: ");
    Serial.println(configLoadStatus);
    return false;
  }

  FILE* fp = fopen("/user/config.ini", "r");
  if (fp == nullptr) {
    configLoadStatus = "mounted /user but /user/config.ini not found";
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
    } else if (key.equalsIgnoreCase("POST_PATH")) {
      setConfigValue(POST_PATH, sizeof(POST_PATH), value);
      loadedKeys++;
    }
  }

  fclose(fp);

  configLoadStatus = "loaded " + String(loadedKeys) + " keys from /user/config.ini";
  return loadedKeys > 0;
}

void printRuntimeConfigSummary() {
  Serial.println("Runtime config summary:");
  Serial.print("  user fs mounted: ");
  Serial.println(userFsMounted ? "true" : "false");
  Serial.print("  user fs type: ");
  Serial.println(userFsType);
  Serial.print("  user fs partition: ");
  Serial.println(userFsPartition);
  Serial.print("  config status: ");
  Serial.println(configLoadStatus);

  Serial.print("  WIFI_SSID set: ");
  Serial.println(isPlaceholder(WIFI_SSID, "YOUR_WIFI_SSID") ? "no" : "yes");
  Serial.print("  WIFI_PASS set: ");
  Serial.println(isPlaceholder(WIFI_PASS, "YOUR_WIFI_PASSWORD") ? "no" : "yes");
  Serial.print("  DEVICE_ID: ");
  Serial.println(DEVICE_ID);
  Serial.print("  SHARED_SECRET set: ");
  Serial.println(isPlaceholder(SHARED_SECRET, "PUT_A_LONG_RANDOM_SECRET_HERE") ? "no" : "yes");
  Serial.print("  POST_PATH set: ");
  Serial.println(isPlaceholder(POST_PATH, "/macros/s/PUT_YOUR_DEPLOYMENT_ID_HERE/exec") ? "no" : "yes");
}
