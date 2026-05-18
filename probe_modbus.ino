// Manual Modbus RTU reader for the Solinst 301.
//
// This intentionally does not use ArduinoModbus / ModbusRTUClient. During Opta
// bring-up we found that the Opta RS485 object can echo the transmitted request
// into the receive buffer, with the actual Solinst response following after the
// echo. The reader below forces RS485 back to receive mode after transmit,
// keeps waiting after echo-only data, and searches the RX buffer for a valid
// Modbus response frame.

bool solinstRs485Started = false;

uint16_t modbusCrc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;

  for (size_t pos = 0; pos < len; pos++) {
    crc ^= data[pos];
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x0001) {
        crc >>= 1;
        crc ^= 0xA001;
      } else {
        crc >>= 1;
      }
    }
  }

  return crc;
}

void printHexByte(uint8_t value) {
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
}

void printModbusBytes(const char *label, const uint8_t *data, size_t len) {
  Serial.print(label);
  Serial.print(" [");
  Serial.print(len);
  Serial.print(" bytes]: ");

  if (len == 0) {
    Serial.println("<none>");
    return;
  }

  for (size_t i = 0; i < len; i++) {
    if (i > 0) Serial.print(' ');
    printHexByte(data[i]);
  }
  Serial.println();
}

void beginSolinstRs485() {
  if (solinstRs485Started) return;

  RS485.end();
  delay(50);
  RS485.setDelays(WLTS_RS485_PRE_DELAY_US, WLTS_RS485_POST_DELAY_US);
  RS485.begin(WLTS_BAUD, WLTS_SERIAL_CFG);
  RS485.receive();

  solinstRs485Started = true;

  Serial.print("Solinst RS-485 initialized: baud=");
  Serial.print(WLTS_BAUD);
  Serial.print(" pre=");
  Serial.print(WLTS_RS485_PRE_DELAY_US);
  Serial.print("us post=");
  Serial.print(WLTS_RS485_POST_DELAY_US);
  Serial.println("us echo-aware manual Modbus");
}

void clearRs485ReceiveBuffer() {
  unsigned long start = millis();
  while (millis() - start < 20) {
    while (RS485.available() > 0) {
      RS485.read();
      start = millis();
    }
  }
}

void rs485WriteBytes(const uint8_t *data, size_t len) {
  RS485.beginTransmission();
  RS485.write(data, len);
  RS485.endTransmission();

  // On the Opta, the 301 replies quickly. Force RX immediately after TX.
  RS485.receive();
}

const char *validateModbusResponse(const uint8_t *response, size_t length,
                                   uint8_t slaveId, uint8_t functionCode,
                                   uint8_t expectedByteCount) {
  if (length == 0) return "no response";
  if (length < 5) return "response too short";
  if (response[0] != slaveId) return "wrong slave id in response";

  uint16_t receivedCrc = (uint16_t)response[length - 2] | ((uint16_t)response[length - 1] << 8);
  uint16_t calculatedCrc = modbusCrc16(response, length - 2);
  if (receivedCrc != calculatedCrc) return "CRC mismatch";

  if (response[1] & 0x80) return "Modbus exception response";
  if (response[1] != functionCode) return "wrong function code in response";
  if (response[2] != expectedByteCount) return "unexpected byte count";
  if (length != (size_t)(expectedByteCount + 5)) return "unexpected response length";

  return nullptr;
}

int findValidResponseOffset(const uint8_t *buffer, size_t length,
                            uint8_t slaveId, uint8_t functionCode,
                            uint8_t expectedByteCount) {
  const size_t expectedLength = (size_t)expectedByteCount + 5;
  if (length < expectedLength) return -1;

  for (size_t offset = 0; offset + expectedLength <= length; offset++) {
    const uint8_t *candidate = buffer + offset;
    if (candidate[0] != slaveId) continue;
    if (candidate[1] != functionCode) continue;
    if (candidate[2] != expectedByteCount) continue;

    if (validateModbusResponse(candidate, expectedLength, slaveId, functionCode, expectedByteCount) == nullptr) {
      return (int)offset;
    }
  }

  return -1;
}

size_t readRawResponseUntilCandidate(uint8_t *buffer, size_t bufferSize,
                                     unsigned long timeoutMs,
                                     uint8_t slaveId, uint8_t functionCode,
                                     uint8_t expectedByteCount,
                                     int &responseOffset) {
  size_t length = 0;
  responseOffset = -1;
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    while (RS485.available() > 0) {
      int b = RS485.read();
      if (b < 0) continue;
      if (length < bufferSize) {
        buffer[length++] = (uint8_t)b;
      }
    }

    responseOffset = findValidResponseOffset(buffer, length, slaveId, functionCode, expectedByteCount);
    if (responseOffset >= 0) {
      return length;
    }

    // Do not stop on an inter-byte gap. The first bytes may be only the Opta's
    // echoed TX frame; the actual 301 response may arrive later.
    delay(1);
  }

  return length;
}

bool readInputRegistersOnce(uint8_t slaveId, uint16_t startReg, uint16_t quantity,
                            uint16_t *values, const char **errorOut) {
  if (quantity == 0 || quantity > 16) {
    if (errorOut) *errorOut = "unsupported quantity";
    return false;
  }

  beginSolinstRs485();

  uint8_t request[8];
  request[0] = slaveId;
  request[1] = 0x04;
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
  const size_t expectedFrameLength = (size_t)expectedByteCount + 5;
  int responseOffset = -1;

  size_t responseLength = readRawResponseUntilCandidate(
    responseBuffer,
    sizeof(responseBuffer),
    WLTS_RESPONSE_TIMEOUT_MS,
    slaveId,
    0x04,
    expectedByteCount,
    responseOffset
  );

  if (responseOffset < 0) {
    if (errorOut) {
      *errorOut = validateModbusResponse(responseBuffer, responseLength, slaveId, 0x04, expectedByteCount);
      if (*errorOut == nullptr) *errorOut = "no valid response candidate found in RX buffer";
    }

    Serial.print("Modbus read failed: slave=");
    Serial.print(slaveId);
    Serial.print(" reg=0x");
    Serial.print(startReg, HEX);
    Serial.print(" qty=");
    Serial.print(quantity);
    Serial.print(" err=");
    Serial.println(errorOut && *errorOut ? *errorOut : "unknown");
    printModbusBytes("RX raw", responseBuffer, responseLength);
    return false;
  }

  const uint8_t *response = responseBuffer + responseOffset;
  if (responseOffset > 0) {
    Serial.print("Modbus RX ignored Opta TX echo/noise prefix of ");
    Serial.print(responseOffset);
    Serial.println(" byte(s)");
  }

  for (uint16_t i = 0; i < quantity; i++) {
    size_t offset = 3 + (i * 2);
    values[i] = ((uint16_t)response[offset] << 8) | response[offset + 1];
  }

  if (errorOut) *errorOut = nullptr;
  (void)expectedFrameLength;
  return true;
}

bool readInputRegistersWithRetry(uint8_t slaveId, uint16_t startReg, uint16_t quantity, uint16_t *values) {
  unsigned long backoff = INITIAL_BACKOFF_MS;
  const char *lastError = nullptr;

  for (int attempt = 1; attempt <= READ_RETRIES; attempt++) {
    if (readInputRegistersOnce(slaveId, startReg, quantity, values, &lastError)) {
      return true;
    }

    Serial.print("Read failed: slave=");
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

  return false;
}

bool readInputRegister(uint8_t slaveId, uint16_t reg, uint16_t &value) {
  uint16_t values[1];
  if (!readInputRegistersWithRetry(slaveId, reg, 1, values)) {
    return false;
  }
  value = values[0];
  return true;
}

bool readInputRegisterWithRetry(uint8_t slaveId, uint16_t reg, uint16_t &value) {
  return readInputRegister(slaveId, reg, value);
}

bool readRegisterPairWithRetry(uint8_t slaveId, uint16_t regHi, uint16_t regLo,
                               uint16_t &hi, uint16_t &lo) {
  if (regLo == regHi + 1) {
    uint16_t values[2];
    if (!readInputRegistersWithRetry(slaveId, regHi, 2, values)) return false;
    hi = values[0];
    lo = values[1];
    return true;
  }

  if (!readInputRegisterWithRetry(slaveId, regHi, hi)) return false;
  if (!readInputRegisterWithRetry(slaveId, regLo, lo)) return false;
  return true;
}

bool readSensorIdentity(uint8_t slaveId, SensorIdentity &id) {
  uint16_t fwVersionRaw = 0;
  uint16_t fwBeta = 0;
  uint16_t serialRegs[2] = {0, 0};

  if (!readInputRegisterWithRetry(slaveId, REG_FW_VERSION, fwVersionRaw)) return false;
  if (!readInputRegisterWithRetry(slaveId, REG_FW_BETA, fwBeta)) return false;
  if (!readInputRegistersWithRetry(slaveId, REG_SERIAL_HI, 2, serialRegs)) return false;

  id.fwMajor = (fwVersionRaw >> 8) & 0xFF;
  id.fwMinor = fwVersionRaw & 0xFF;
  id.fwBeta = fwBeta;
  id.serialNumber = regsToUint32(serialRegs[0], serialRegs[1]);

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
  uint16_t regs[4] = {0, 0, 0, 0};

  if (!readInputRegistersWithRetry(slaveId, REG_LEVEL_HI, 4, regs)) return false;

  reading.level = regsToFloat(regs[0], regs[1]);
  reading.temperature = regsToFloat(regs[2], regs[3]);
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
