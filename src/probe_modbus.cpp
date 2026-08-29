#include "config.h"

// Manual Modbus RTU reader for the Solinst 301.
//
// This intentionally does not use ArduinoModbus / ModbusRTUClient. During Opta
// bring-up we found that the Opta RS485 object can echo the transmitted request
// into the receive buffer, with the actual Solinst response following after the
// echo. The reader below forces RS485 back to receive mode after transmit,
// keeps waiting after echo-only data, and searches the RX buffer for a valid
// Modbus response frame.

uint16_t modbusCrc16(const uint8_t *data, size_t len) {
  return logger_core::modbusCrc16(data, len);
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

void beginRs485(Rs485Channel &channel, uint32_t baud,
                uint16_t serialConfig, const char *deviceName) {
  const bool started = channel.begin(
      baud, serialConfig, WLTS_RS485_PRE_DELAY_US,
      WLTS_RS485_POST_DELAY_MARGIN_US);
  Serial.print(deviceName);
  Serial.print(" RS-485 initialized: baud=");
  Serial.print(baud);
  Serial.print(" channel=");
  Serial.print(channel.name());
  Serial.print(" status=");
  Serial.println(started ? "ready" : "failed");
}

void beginSolinstRs485() {
  beginRs485(solinstRs485Channel(), WLTS_BAUD, WLTS_SERIAL_CFG, "Solinst");
}

void beginWeatherRs485() {
  beginRs485(
      weatherRs485Channel(), WEATHER_BAUD, WEATHER_SERIAL_CFG, "Weather");
}

void clearRs485ReceiveBuffer(Rs485Channel &channel) {
  channel.clearReceive(20, 100);
}

bool rs485WriteBytes(
    Rs485Channel &channel, const uint8_t *data, size_t len) {
  return channel.write(data, len);
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
  return logger_core::findValidModbusResponse(
      buffer, length, slaveId, functionCode, expectedByteCount);
}

size_t readRawResponseUntilCandidate(Rs485Channel &channel,
                                     uint8_t *buffer, size_t bufferSize,
                                     unsigned long timeoutMs,
                                     uint8_t slaveId, uint8_t functionCode,
                                     uint8_t expectedByteCount,
                                     int &responseOffset) {
  size_t length = 0;
  responseOffset = -1;
  unsigned long start = millis();

  while (millis() - start < timeoutMs) {
    while (channel.available() > 0) {
      int b = channel.read();
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
  Rs485Channel &channel = solinstRs485Channel();

  uint8_t request[logger_core::MODBUS_READ_REQUEST_BYTES];
  if (logger_core::buildModbusReadRequest(
          slaveId, 0x04, startReg, quantity, request, sizeof(request)) == 0) {
    if (errorOut) *errorOut = "invalid Modbus read request";
    return false;
  }

  clearRs485ReceiveBuffer(channel);
  if (!rs485WriteBytes(channel, request, sizeof(request))) {
    if (errorOut) *errorOut = "failed to write Modbus request";
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

  if (responseOffset > 0) {
    Serial.print("Modbus RX ignored Opta TX echo/noise prefix of ");
    Serial.print(responseOffset);
    Serial.println(" byte(s)");
  }

  size_t decodedOffset = 0;
  if (logger_core::decodeModbusReadRegisters(
          responseBuffer, responseLength, slaveId, 0x04, quantity, values,
          quantity, &decodedOffset) != logger_core::ModbusDecodeResult::OK ||
      decodedOffset != static_cast<size_t>(responseOffset)) {
    if (errorOut) *errorOut = "failed to decode validated Modbus response";
    return false;
  }

  if (errorOut) *errorOut = nullptr;
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
  return logger_core::sensorIdentityValid(
      id.serialNumber, id.fwMajor, id.fwMinor, id.fwBeta);
}

bool scanForSensor(uint8_t startId, uint8_t endId, uint8_t &foundId, SensorIdentity &foundIdentity) {
  if (WLTS_USE_FIXED_MODBUS_ID) {
    Serial.print("Trying configured Solinst Modbus ID ");
    Serial.println(WLTS_FIXED_MODBUS_ID);

    SensorIdentity candidate;
    if (readSensorIdentity(WLTS_FIXED_MODBUS_ID, candidate) && identityLooksValid(candidate)) {
      foundId = WLTS_FIXED_MODBUS_ID;
      foundIdentity = candidate;
      return true;
    }

    Serial.print("Configured Solinst Modbus ID did not respond: ");
    Serial.println(WLTS_FIXED_MODBUS_ID);
    return false;
  }

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

bool attemptSensorDiscovery() {
  const bool found = scanForSensor(
      SCAN_START_ID, SCAN_END_ID, detectedSensorId, detectedIdentity);
  logger_core::recordSensorDiscoveryResult(
      sensorDiscoveryState, millis(), found,
      SENSOR_DISCOVERY_RETRY_INITIAL_MS, SENSOR_DISCOVERY_RETRY_MAX_MS);

  if (found) {
    printIdentity(detectedSensorId, detectedIdentity);
    return true;
  }

  Serial.print("No Solinst 301 found; consecutive discovery failures: ");
  Serial.print(sensorDiscoveryState.consecutiveFailures);
  Serial.print(", retry in ms: ");
  Serial.println(logger_core::sensorDiscoveryCooldownRemaining(
      millis(), false, sensorDiscoveryState));
  return false;
}

void maintainSensorDiscovery() {
  if (!logger_core::sensorDiscoveryDue(
          millis(), detectedSensorId != 0, sensorDiscoveryState)) {
    return;
  }

  Serial.println("Retrying Solinst sensor discovery");
  attemptSensorDiscovery();
}

bool readLevelAndTemperature(uint8_t slaveId, ProbeReading &reading) {
  uint16_t regs[4] = {0, 0, 0, 0};

  if (!readInputRegistersWithRetry(slaveId, REG_LEVEL_HI, 4, regs)) return false;

  reading.level = regsToFloat(regs[0], regs[1]);
  reading.temperature = regsToFloat(regs[2], regs[3]);
  reading.timestampUtc = nowUtcString();
  reading.valid = isfinite(reading.level) && isfinite(reading.temperature) && reading.timestampUtc.length() > 0;

  if (reading.valid) {
    refreshPowerForReading(reading);
    // A weather failure is recorded in the reading but does not discard a
    // successful lake measurement.
    readWeatherForReading(reading);
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
    reading.modbusId = detectedSensorId;
    reading.sensorIdentity = detectedIdentity;
    lastProbeReading = reading;
    if (siteReadingRevision != UINT32_MAX) {
      ++siteReadingRevision;
    }
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
