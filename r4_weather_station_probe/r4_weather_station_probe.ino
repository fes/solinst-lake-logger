// Read-only DFRobot SEN0657 discovery probe for Arduino Uno R4 WiFi with the
// RS232/RS485 Shield V1 shown in IMG_1900.jpg.
//
// Shield setup:
//   - Select RS485, not RS232.
//   - Select UART, not SoftSerial.
//   - Select Auto, not Manual, direction control.
//
// The shield's UART connects to Uno pins D0/D1. On an Uno R4 WiFi, use Serial1
// for those pins and keep Serial for the USB serial monitor. This sketch scans
// the SEN0657 documented baud rates and Modbus IDs 1-10 without changing any
// station configuration.

constexpr uint16_t REG_DEVICE_ADDRESS = 0x07D0;
constexpr uint16_t REG_WIND_SPEED = 0x01F4;
constexpr uint8_t DISCOVERY_MAX_MODBUS_ID = 10;
constexpr unsigned long DISCOVERY_TIMEOUT_MS = 500UL;

struct BaudSetting {
  uint32_t baud;
  uint16_t registerValue;
};

constexpr BaudSetting BAUD_SETTINGS[] = {
  {1200, 7},
  {2400, 0},
  {4800, 1},
  {9600, 2},
  {19200, 3},
  {38400, 4},
  {57600, 5},
  {115200, 6}
};

uint16_t modbusCrc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t index = 0; index < length; index++) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

void printHexBytes(const char *label, const uint8_t *data, size_t length) {
  Serial.print(label);
  Serial.print(" [");
  Serial.print(length);
  Serial.print(" bytes]:");
  if (length == 0) {
    Serial.println(" <none>");
    return;
  }

  for (size_t index = 0; index < length; index++) {
    Serial.print(" ");
    if (data[index] < 0x10) Serial.print("0");
    Serial.print(data[index], HEX);
  }
  Serial.println();
}

void beginRs485(uint32_t baud) {
  Serial1.begin(baud, SERIAL_8N1);
  delay(50);
  Serial.print("RS-485 configured: ");
  Serial.print(baud);
  Serial.println(" baud, 8N1");
}

void clearReceiveBuffer() {
  unsigned long lastByteMs = millis();
  while (millis() - lastByteMs < 20) {
    while (Serial1.available() > 0) {
      Serial1.read();
      lastByteMs = millis();
    }
  }
}

bool readHoldingRegisters(uint8_t slaveId, uint16_t startRegister, uint16_t quantity,
                          uint16_t *values) {
  uint8_t request[8] = {
    slaveId,
    0x03,
    highByte(startRegister),
    lowByte(startRegister),
    highByte(quantity),
    lowByte(quantity),
    0,
    0
  };
  uint16_t crc = modbusCrc16(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  clearReceiveBuffer();
  printHexBytes("TX", request, sizeof(request));
  Serial1.write(request, sizeof(request));
  Serial1.flush();

  const uint8_t byteCount = quantity * 2;
  const size_t expectedLength = byteCount + 5;
  uint8_t buffer[64];
  size_t length = 0;
  unsigned long startMs = millis();

  while (millis() - startMs < DISCOVERY_TIMEOUT_MS) {
    while (Serial1.available() > 0 && length < sizeof(buffer)) {
      int received = Serial1.read();
      if (received >= 0) buffer[length++] = (uint8_t)received;
    }

    for (size_t offset = 0; offset + expectedLength <= length; offset++) {
      const uint8_t *response = buffer + offset;
      if (response[0] != slaveId || response[1] != 0x03 || response[2] != byteCount) continue;

      uint16_t receivedCrc = response[expectedLength - 2] |
                             ((uint16_t)response[expectedLength - 1] << 8);
      if (receivedCrc != modbusCrc16(response, expectedLength - 2)) continue;

      for (uint16_t index = 0; index < quantity; index++) {
        values[index] = ((uint16_t)response[3 + (index * 2)] << 8) |
                        response[4 + (index * 2)];
      }
      printHexBytes("RX", buffer, length);
      return true;
    }
    delay(1);
  }

  printHexBytes("RX", buffer, length);
  return false;
}

bool findWeatherStation(uint8_t &foundId, BaudSetting &foundBaud) {
  for (const BaudSetting &candidateBaud : BAUD_SETTINGS) {
    beginRs485(candidateBaud.baud);
    for (uint8_t candidateId = 1; candidateId <= DISCOVERY_MAX_MODBUS_ID; candidateId++) {
      uint16_t registers[2];
      if (!readHoldingRegisters(candidateId, REG_DEVICE_ADDRESS, 2, registers)) continue;
      if (registers[0] != candidateId) continue;

      foundId = candidateId;
      foundBaud = candidateBaud;
      foundBaud.registerValue = registers[1];
      return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("Uno R4 WiFi + RS232/RS485 Shield V1 SEN0657 probe");
  Serial.println("Shield must be set to RS485, UART, and Auto.");

  uint8_t stationId = 0;
  BaudSetting stationBaud = {0, 0};
  if (!findWeatherStation(stationId, stationBaud)) {
    Serial.println("FAILED: no SEN0657 response at documented baud rates or IDs 1-10");
    return;
  }

  Serial.print("FOUND: ID ");
  Serial.print(stationId);
  Serial.print(", baud ");
  Serial.print(stationBaud.baud);
  Serial.print(", reported baud code ");
  Serial.println(stationBaud.registerValue);

  uint16_t windSpeed[1];
  if (readHoldingRegisters(stationId, REG_WIND_SPEED, 1, windSpeed)) {
    Serial.print("Wind speed: ");
    Serial.print(windSpeed[0] / 10.0f);
    Serial.println(" m/s");
  } else {
    Serial.println("Station was discovered, but the wind-speed read failed.");
  }
}

void loop() {
  delay(1000);
}
