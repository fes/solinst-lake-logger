// RS-485 diagnostic for Arduino Opta and a DFRobot SEN0657.
//
// Disconnect the Solinst. Master mode alternates between the SEN0657 factory
// endpoint (ID 1 / 4800 8N1) and provisioned endpoint (ID 2 / 19200 8N1).
// Select exactly one master on the bus:
//   - Set this sketch to MODE_MASTER and the Uno R4 sketch to MODE_MONITOR, or
//   - Set this sketch to MODE_MONITOR and the Uno R4 sketch to MODE_MASTER.
//
// All nodes must share RS-485 A, B, and common/reference.

#include <ArduinoRS485.h>

constexpr uint8_t MODE_MASTER = 1;
constexpr uint8_t MODE_MONITOR = 2;
constexpr uint8_t DIAGNOSTIC_MODE = MODE_MASTER;

struct WeatherStationProfile {
  const char *label;
  uint32_t baud;
  uint8_t modbusId;
};

constexpr WeatherStationProfile WEATHER_PROFILES[] = {
  {"factory", 4800, 1},
  {"provisioned", 19200, 2}
};
// A passive UART monitor can receive only one baud rate at a time. Change this
// to 1 after provisioning the station.
constexpr size_t MONITOR_PROFILE_INDEX = 0;
constexpr uint16_t WIND_SPEED_REGISTER = 0x01F4;
constexpr unsigned long MASTER_INTERVAL_MS = 3000UL;
constexpr unsigned long RESPONSE_TIMEOUT_MS = 1000UL;
constexpr unsigned long FRAME_GAP_MS = 12UL;
constexpr unsigned long MONITOR_HEARTBEAT_MS = 5000UL;

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
  Serial.print("t=");
  Serial.print(millis());
  Serial.print(" ");
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

void printFrameDiagnostics(const uint8_t *data, size_t length) {
  if (length == 0) {
    Serial.println("  RESULT: no received bytes");
    return;
  }

  for (size_t offset = 0; offset < length;) {
    size_t remaining = length - offset;
    if (remaining >= 8 && data[offset + 1] == 0x03) {
      uint16_t receivedCrc = data[offset + 6] | ((uint16_t)data[offset + 7] << 8);
      if (receivedCrc == modbusCrc16(data + offset, 6)) {
        uint16_t registerAddress = ((uint16_t)data[offset + 2] << 8) | data[offset + 3];
        uint16_t quantity = ((uint16_t)data[offset + 4] << 8) | data[offset + 5];
        Serial.print("  FRAME @");
        Serial.print(offset);
        Serial.print(": request slave=");
        Serial.print(data[offset]);
        Serial.print(" function=0x03 register=0x");
        Serial.print(registerAddress, HEX);
        Serial.print(" quantity=");
        Serial.print(quantity);
        Serial.println(" CRC=OK");
        offset += 8;
        continue;
      }
    }

    if (remaining >= 5 && (data[offset + 1] & 0x80)) {
      uint16_t receivedCrc = data[offset + 3] | ((uint16_t)data[offset + 4] << 8);
      Serial.print("  FRAME @");
      Serial.print(offset);
      Serial.print(": exception slave=");
      Serial.print(data[offset]);
      Serial.print(" function=0x");
      Serial.print(data[offset + 1], HEX);
      Serial.print(" code=0x");
      Serial.print(data[offset + 2], HEX);
      Serial.println(receivedCrc == modbusCrc16(data + offset, 3) ? " CRC=OK" : " CRC=BAD");
      offset += 5;
      continue;
    }

    if (remaining >= 5 && data[offset + 1] == 0x03) {
      uint8_t byteCount = data[offset + 2];
      size_t frameLength = byteCount + 5;
      if (remaining >= frameLength) {
        uint16_t receivedCrc = data[offset + frameLength - 2] |
                               ((uint16_t)data[offset + frameLength - 1] << 8);
        Serial.print("  FRAME @");
        Serial.print(offset);
        Serial.print(": response slave=");
        Serial.print(data[offset]);
        Serial.print(" function=0x03 byte_count=");
        Serial.print(byteCount);
        Serial.println(receivedCrc == modbusCrc16(data + offset, frameLength - 2) ? " CRC=OK" : " CRC=BAD");
        offset += frameLength;
        continue;
      }
    }

    Serial.print("  FRAME @");
    Serial.print(offset);
    Serial.println(": unrecognized/truncated byte");
    offset++;
  }
}

bool extractWindSpeedResponse(const uint8_t *data, size_t length, uint8_t slaveId, float &windSpeedMs) {
  for (size_t offset = 0; offset + 7 <= length; offset++) {
    if (data[offset] != slaveId || data[offset + 1] != 0x03 || data[offset + 2] != 2) continue;
    uint16_t receivedCrc = data[offset + 5] | ((uint16_t)data[offset + 6] << 8);
    if (receivedCrc != modbusCrc16(data + offset, 5)) continue;
    windSpeedMs = (((uint16_t)data[offset + 3] << 8) | data[offset + 4]) / 10.0f;
    return true;
  }
  return false;
}

void clearReceiveBuffer() {
  while (RS485.available() > 0) RS485.read();
}

unsigned long rs485PostDelayUs(uint32_t baud) {
  return ((12000000UL + baud - 1) / baud) + 500UL;
}

void configureRs485(const WeatherStationProfile &profile) {
  RS485.end();
  delay(20);
  RS485.setDelays(1000, rs485PostDelayUs(profile.baud));
  RS485.begin(profile.baud, SERIAL_8N1);
  RS485.receive();

  Serial.print("RS-485 profile: ");
  Serial.print(profile.label);
  Serial.print(" (ID ");
  Serial.print(profile.modbusId);
  Serial.print(", ");
  Serial.print(profile.baud);
  Serial.println(" baud, 8N1)");
}

void sendWindSpeedRequest(const WeatherStationProfile &profile) {
  uint8_t request[8] = {
    profile.modbusId,
    0x03,
    highByte(WIND_SPEED_REGISTER),
    lowByte(WIND_SPEED_REGISTER),
    0x00,
    0x01,
    0,
    0
  };
  uint16_t crc = modbusCrc16(request, 6);
  request[6] = lowByte(crc);
  request[7] = highByte(crc);

  clearReceiveBuffer();
  printHexBytes("TX", request, sizeof(request));
  RS485.beginTransmission();
  RS485.write(request, sizeof(request));
  RS485.endTransmission();
  RS485.receive();

  uint8_t response[32];
  size_t responseLength = 0;
  unsigned long startMs = millis();
  while (millis() - startMs < RESPONSE_TIMEOUT_MS) {
    while (RS485.available() > 0 && responseLength < sizeof(response)) {
      int received = RS485.read();
      if (received >= 0) response[responseLength++] = (uint8_t)received;
    }
    delay(1);
  }
  printHexBytes("RX", response, responseLength);
  printFrameDiagnostics(response, responseLength);
  float windSpeedMs = NAN;
  if (extractWindSpeedResponse(response, responseLength, profile.modbusId, windSpeedMs)) {
    Serial.print("  RESULT: valid SEN0657 wind-speed response = ");
    Serial.print(windSpeedMs, 1);
    Serial.println(" m/s");
  } else {
    Serial.println("  RESULT: no valid SEN0657 wind-speed response before timeout");
  }
}

void monitorBus() {
  static uint8_t frame[64];
  static size_t frameLength = 0;
  static unsigned long lastByteMs = 0;
  static unsigned long lastCompleteFrameMs = 0;
  static unsigned long lastHeartbeatMs = 0;

  while (RS485.available() > 0) {
    int received = RS485.read();
    if (received >= 0) {
      if (frameLength < sizeof(frame)) {
        frame[frameLength++] = (uint8_t)received;
      } else {
        Serial.println("MONITOR: frame buffer overflow; discarding current frame");
        frameLength = 0;
      }
      lastByteMs = millis();
    }
  }

  if (frameLength > 0 && millis() - lastByteMs >= FRAME_GAP_MS) {
    printHexBytes("BUS", frame, frameLength);
    printFrameDiagnostics(frame, frameLength);
    frameLength = 0;
    lastCompleteFrameMs = millis();
  }

  if (millis() - lastHeartbeatMs >= MONITOR_HEARTBEAT_MS &&
      (lastCompleteFrameMs == 0 || millis() - lastCompleteFrameMs >= MONITOR_HEARTBEAT_MS)) {
    lastHeartbeatMs = millis();
    Serial.print("t=");
    Serial.print(lastHeartbeatMs);
    const WeatherStationProfile &profile = WEATHER_PROFILES[MONITOR_PROFILE_INDEX];
    Serial.print(" MONITOR: listening at ");
    Serial.print(profile.baud);
    Serial.println(" 8N1; no complete frame since last heartbeat");
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("Opta SEN0657 RS-485 diagnostic");
  Serial.print("Mode: ");
  Serial.println(DIAGNOSTIC_MODE == MODE_MASTER ? "MASTER" : "MONITOR");
  Serial.println("Master alternates factory ID 1 / 4800 and provisioned ID 2 / 19200, both 8N1.");
  if (DIAGNOSTIC_MODE == MODE_MONITOR) {
    configureRs485(WEATHER_PROFILES[MONITOR_PROFILE_INDEX]);
  }
}

void loop() {
  if (DIAGNOSTIC_MODE == MODE_MASTER) {
    static unsigned long lastRequestMs = 0;
    static size_t profileIndex = 0;
    if (millis() - lastRequestMs >= MASTER_INTERVAL_MS) {
      lastRequestMs = millis();
      const WeatherStationProfile &profile = WEATHER_PROFILES[profileIndex];
      configureRs485(profile);
      sendWindSpeedRequest(profile);
      profileIndex = (profileIndex + 1) % (sizeof(WEATHER_PROFILES) / sizeof(WEATHER_PROFILES[0]));
    }
  } else {
    monitorBus();
  }
}
