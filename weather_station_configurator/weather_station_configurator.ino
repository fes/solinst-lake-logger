// One-time DFRobot SEN0657 weather-station provisioning firmware for Arduino Opta.
//
// Disconnect the Solinst 301 from RS-485 before uploading/running this sketch.
// The factory weather station is Modbus ID 1, which conflicts with the Solinst.
//
// This sketch scans the documented baud rates and Modbus IDs 1-10 to discover
// the station, changes it to ID 2, changes it to 19200 baud, then verifies both
// settings. It does not alter the station's documented 8N1 framing.

#include <ArduinoRS485.h>

constexpr uint8_t FACTORY_MODBUS_ID = 1;
constexpr uint8_t TARGET_MODBUS_ID = 2;
constexpr uint32_t FACTORY_BAUD = 4800;
constexpr uint32_t TARGET_BAUD = 19200;
constexpr uint16_t REG_DEVICE_ADDRESS = 0x07D0;
constexpr uint16_t REG_BAUD_RATE = 0x07D1;
constexpr uint16_t TARGET_BAUD_REGISTER_VALUE = 3;
constexpr unsigned long RESPONSE_TIMEOUT_MS = 1500UL;
constexpr unsigned long DISCOVERY_RESPONSE_TIMEOUT_MS = 500UL;
constexpr uint8_t DISCOVERY_MAX_MODBUS_ID = 10;
constexpr unsigned long RS485_POST_DELAY_MARGIN_US = 500UL;

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

unsigned long rs485PostDelayUs(uint32_t baud) {
  // Allow 12 bit-times plus a margin after flush without delaying an immediate
  // weather-station reply longer than necessary.
  return ((12000000UL + baud - 1) / baud) + RS485_POST_DELAY_MARGIN_US;
}

void beginRs485(uint32_t baud) {
  RS485.end();
  delay(50);
  RS485.setDelays(1000, rs485PostDelayUs(baud));
  RS485.begin(baud, SERIAL_8N1);
  RS485.receive();
  Serial.print("RS-485 configured: ");
  Serial.print(baud);
  Serial.println(" baud, 8N1");
}

void clearReceiveBuffer() {
  unsigned long lastByteMs = millis();
  while (millis() - lastByteMs < 20) {
    while (RS485.available() > 0) {
      RS485.read();
      lastByteMs = millis();
    }
  }
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

void sendFrame(const uint8_t *frame, size_t length) {
  clearReceiveBuffer();
  printHexBytes("TX", frame, length);
  RS485.beginTransmission();
  RS485.write(frame, length);
  RS485.endTransmission();
  RS485.receive();
}

bool readHoldingRegisters(uint8_t slaveId, uint16_t startRegister, uint16_t quantity,
                          uint16_t *values, unsigned long timeoutMs = RESPONSE_TIMEOUT_MS) {
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
  sendFrame(request, sizeof(request));

  const uint8_t byteCount = quantity * 2;
  const size_t expectedLength = byteCount + 5;
  uint8_t buffer[64];
  size_t length = 0;
  unsigned long startMs = millis();

  while (millis() - startMs < timeoutMs) {
    while (RS485.available() > 0 && length < sizeof(buffer)) {
      int value = RS485.read();
      if (value >= 0) buffer[length++] = (uint8_t)value;
    }

    for (size_t offset = 0; offset + expectedLength <= length; offset++) {
      const uint8_t *response = buffer + offset;
      if (response[0] != slaveId || response[1] != 0x03 || response[2] != byteCount) continue;
      uint16_t receivedCrc = response[expectedLength - 2] | ((uint16_t)response[expectedLength - 1] << 8);
      if (receivedCrc != modbusCrc16(response, expectedLength - 2)) continue;

      for (uint16_t index = 0; index < quantity; index++) {
        values[index] = ((uint16_t)response[3 + (index * 2)] << 8) | response[4 + (index * 2)];
      }
      printHexBytes("RX", buffer, length);
      return true;
    }
    delay(1);
  }

  printHexBytes("RX", buffer, length);
  return false;
}

void writeSingleRegister(uint8_t slaveId, uint16_t registerAddress, uint16_t value) {
  uint8_t request[8] = {
    slaveId,
    0x06,
    highByte(registerAddress),
    lowByte(registerAddress),
    highByte(value),
    lowByte(value),
    0,
    0
  };
  uint16_t crc = modbusCrc16(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  // On the Opta, TX bytes can echo into RX. A 0x06 response exactly matches its
  // request, so verify writes by reading the target register afterward instead.
  sendFrame(request, sizeof(request));

  uint8_t response[32];
  size_t responseLength = 0;
  unsigned long startMs = millis();
  while (millis() - startMs < DISCOVERY_RESPONSE_TIMEOUT_MS) {
    while (RS485.available() > 0 && responseLength < sizeof(response)) {
      int received = RS485.read();
      if (received >= 0) response[responseLength++] = (uint8_t)received;
    }
    delay(1);
  }
  printHexBytes("RX", response, responseLength);
}

bool verifySettings(uint8_t slaveId, uint16_t expectedAddress, uint16_t expectedBaud) {
  uint16_t registers[2];
  if (!readHoldingRegisters(slaveId, REG_DEVICE_ADDRESS, 2, registers)) {
    Serial.println("Verification read failed");
    return false;
  }

  Serial.print("Read address=");
  Serial.print(registers[0]);
  Serial.print(", baud code=");
  Serial.println(registers[1]);
  return registers[0] == expectedAddress && registers[1] == expectedBaud;
}

bool discoverWeatherStation(uint8_t &foundId, BaudSetting &foundBaud) {
  Serial.println("Scanning documented weather-station baud rates and IDs 1-10...");
  for (const BaudSetting &candidateBaud : BAUD_SETTINGS) {
    beginRs485(candidateBaud.baud);
    for (uint8_t candidateId = 1; candidateId <= DISCOVERY_MAX_MODBUS_ID; candidateId++) {
      uint16_t registers[2];
      if (!readHoldingRegisters(candidateId, REG_DEVICE_ADDRESS, 2, registers,
                                DISCOVERY_RESPONSE_TIMEOUT_MS)) {
        continue;
      }

      if (registers[0] != candidateId) {
        Serial.print("Ignoring response with inconsistent address register: ");
        Serial.println(registers[0]);
        continue;
      }

      foundId = candidateId;
      foundBaud = candidateBaud;
      foundBaud.registerValue = registers[1];
      Serial.print("Discovered station: ID ");
      Serial.print(foundId);
      Serial.print(", ");
      Serial.print(foundBaud.baud);
      Serial.print(" baud, reported baud code ");
      Serial.println(registers[1]);
      return true;
    }
  }
  return false;
}

void haltWithFailure(const char *message) {
  Serial.print("FAILED: ");
  Serial.println(message);
  while (true) delay(1000);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("SEN0657 one-time configurator");
  Serial.println("Confirm the Solinst is disconnected from RS-485 before continuing.");

  uint8_t currentId = 0;
  BaudSetting currentBaud = {0, 0};
  if (!discoverWeatherStation(currentId, currentBaud)) {
    haltWithFailure("no SEN0657 response at documented baud rates or IDs 1-10");
  }

  if (currentId != TARGET_MODBUS_ID) {
    Serial.println("Changing weather station address to 2...");
    writeSingleRegister(currentId, REG_DEVICE_ADDRESS, TARGET_MODBUS_ID);
    delay(300);
    if (!verifySettings(TARGET_MODBUS_ID, TARGET_MODBUS_ID, currentBaud.registerValue)) {
      haltWithFailure("address change was not verified");
    }
    currentId = TARGET_MODBUS_ID;
  }

  if (currentBaud.baud != TARGET_BAUD || currentBaud.registerValue != TARGET_BAUD_REGISTER_VALUE) {
    Serial.println("Changing weather station baud rate to 19200...");
    writeSingleRegister(currentId, REG_BAUD_RATE, TARGET_BAUD_REGISTER_VALUE);
    delay(500);
  }

  beginRs485(TARGET_BAUD);
  if (!verifySettings(currentId, TARGET_MODBUS_ID, TARGET_BAUD_REGISTER_VALUE)) {
    haltWithFailure("19200 baud setting was not verified");
  }

  Serial.println("SUCCESS: weather station is Modbus ID 2 at 19200 baud, 8N1.");
  Serial.println("Disconnect power, reconnect the Solinst, and upload the main logger with weather enabled.");
}

void loop() {
  delay(1000);
}
