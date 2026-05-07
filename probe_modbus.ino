bool readInputRegister(uint8_t slaveId, uint16_t reg, uint16_t &value) {
  int v = ModbusRTUClient.inputRegisterRead(slaveId, reg);
  if (v < 0) {
    return false;
  }
  value = (uint16_t)v;
  return true;
}

bool readInputRegisterWithRetry(uint8_t slaveId, uint16_t reg, uint16_t &value) {
  unsigned long backoff = INITIAL_BACKOFF_MS;

  for (int attempt = 1; attempt <= READ_RETRIES; attempt++) {
    if (readInputRegister(slaveId, reg, value)) {
      return true;
    }

    Serial.print("Read failed: slave=");
    Serial.print(slaveId);
    Serial.print(" reg=0x");
    Serial.print(reg, HEX);
    Serial.print(" attempt ");
    Serial.print(attempt);
    Serial.print("/");
    Serial.print(READ_RETRIES);
    Serial.print(" err=");
    Serial.println(ModbusRTUClient.lastError());

    if (attempt < READ_RETRIES) {
      delay(backoff);
      backoff = min(backoff * 2, MAX_BACKOFF_MS);
    }
  }

  return false;
}

bool readRegisterPairWithRetry(uint8_t slaveId, uint16_t regHi, uint16_t regLo,
                               uint16_t &hi, uint16_t &lo) {
  if (!readInputRegisterWithRetry(slaveId, regHi, hi)) return false;
  if (!readInputRegisterWithRetry(slaveId, regLo, lo)) return false;
  return true;
}

bool readSensorIdentity(uint8_t slaveId, SensorIdentity &id) {
  uint16_t fwVersionRaw = 0;
  uint16_t fwBeta = 0;
  uint16_t serialHi = 0;
  uint16_t serialLo = 0;

  if (!readInputRegisterWithRetry(slaveId, REG_FW_VERSION, fwVersionRaw)) return false;
  if (!readInputRegisterWithRetry(slaveId, REG_FW_BETA, fwBeta)) return false;
  if (!readRegisterPairWithRetry(slaveId, REG_SERIAL_HI, REG_SERIAL_LO, serialHi, serialLo)) return false;

  id.fwMajor = (fwVersionRaw >> 8) & 0xFF;
  id.fwMinor = fwVersionRaw & 0xFF;
  id.fwBeta = fwBeta;
  id.serialNumber = regsToUint32(serialHi, serialLo);

  return true;
}

bool identityLooksValid(const SensorIdentity &id) {
  if (id.serialNumber == 0 || id.serialNumber == 0xFFFFFFFFUL) return false;
  if (id.fwMajor == 0 && id.fwMinor == 0) return false;
  return true;
}

bool scanForSensor(uint8_t startId, uint8_t endId, uint8_t &foundId, SensorIdentity &foundIdentity) {
  for (uint8_t id = startId; id <= endId; id++) {
    Serial.print("Scanning Modbus ID ");
    Serial.println(id);

    SensorIdentity candidate;
    if (readSensorIdentity(id, candidate) && identityLooksValid(candidate)) {
      foundId = id;
      foundIdentity = candidate;
      return true;
    }
  }
  return false;
}

bool readLevelAndTemperature(uint8_t slaveId, ProbeReading &reading) {
  uint16_t levelHi = 0, levelLo = 0;
  uint16_t tempHi = 0, tempLo = 0;

  if (!readRegisterPairWithRetry(slaveId, REG_LEVEL_HI, REG_LEVEL_LO, levelHi, levelLo)) return false;
  if (!readRegisterPairWithRetry(slaveId, REG_TEMP_HI, REG_TEMP_LO, tempHi, tempLo)) return false;

  reading.level = regsToFloat(levelHi, levelLo);
  reading.temperature = regsToFloat(tempHi, tempLo);
  reading.timestampUtc = nowUtcString();
  reading.valid = isfinite(reading.level) && isfinite(reading.temperature) && reading.timestampUtc.length() > 0;

  if (reading.valid) {
    readPowerMonitors(reading);
  }

  return reading.valid;
}

bool probeNow(ProbeReading &reading) {
  lastProbeAttemptMs = millis();

  if (detectedSensorId == 0) {
    failedProbeReads++;
    return false;
  }

  if (readLevelAndTemperature(detectedSensorId, reading)) {
    lastProbeReading = reading;
    lastSuccessfulProbeReadUtc = reading.timestampUtc;
    lastSuccessfulProbeReadMs = millis();
    successfulProbeReads++;
    return true;
  }

  failedProbeReads++;
  return false;
}

void printIdentity(uint8_t slaveId, const SensorIdentity &id) {
  Serial.println("Sensor found:");
  Serial.print("  Modbus ID: ");
  Serial.println(slaveId);
  Serial.print("  Serial number: ");
  Serial.println(id.serialNumber);
  Serial.print("  Firmware: v");
  Serial.print(id.fwMajor);
  Serial.print(".");
  Serial.print(id.fwMinor);
  if (id.fwBeta != 0) {
    Serial.print(" beta ");
    Serial.print(id.fwBeta);
  }
  Serial.println();
}
