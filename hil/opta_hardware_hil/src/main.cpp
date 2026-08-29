#include <Arduino.h>
#include <ArduinoRS485.h>
#include <WiFi.h>
#include <Wire.h>

#include <logger_core/modbus_codec.h>

namespace {

constexpr uint32_t CONTROL_BAUD = 115200;
constexpr uint32_t SOLINST_BAUD = 19200;
constexpr uint32_t SOLINST_TIMEOUT_MS = 700;
constexpr uint8_t SOLINST_FIRST_ID = 1;
constexpr uint8_t SOLINST_LAST_ID = 10;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t BATTERY_INA228_ADDRESS = 0x40;
constexpr uint8_t SOLAR_INA228_ADDRESS = 0x41;

char command[16];
size_t commandLength = 0;

bool i2cPresent(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

bool readRegister16(uint8_t address, uint8_t reg, uint16_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(address, static_cast<uint8_t>(2)) != 2) return false;
  value = static_cast<uint16_t>(Wire.read()) << 8U;
  value |= static_cast<uint16_t>(Wire.read());
  return true;
}

void clearRs485() {
  const uint32_t startedMs = millis();
  uint32_t quietSinceMs = startedMs;
  while (millis() - quietSinceMs < 20U &&
         millis() - startedMs < 100U) {
    while (RS485.available() > 0) {
      RS485.read();
      quietSinceMs = millis();
    }
  }
}

bool probeSolinst(uint8_t id, uint16_t* values) {
  uint8_t request[logger_core::MODBUS_READ_REQUEST_BYTES];
  logger_core::buildModbusReadRequest(
      id, 0x04, 0x0000, 10, request, sizeof(request));
  clearRs485();
  RS485.beginTransmission();
  const size_t written = RS485.write(request, sizeof(request));
  RS485.endTransmission();
  RS485.receive();
  if (written != sizeof(request)) return false;

  uint8_t response[96] = {};
  size_t responseLength = 0;
  const uint32_t startedMs = millis();
  while (millis() - startedMs < SOLINST_TIMEOUT_MS) {
    while (RS485.available() > 0) {
      const int byte = RS485.read();
      if (byte >= 0 && responseLength < sizeof(response)) {
        response[responseLength++] = static_cast<uint8_t>(byte);
      }
    }
    size_t offset = 0;
    if (logger_core::decodeModbusReadRegisters(
            response, responseLength, id, 0x04, 10, values, 10,
            &offset) == logger_core::ModbusDecodeResult::OK) {
      return true;
    }
    delay(1);
  }
  return false;
}

void printIna228(uint8_t address) {
  uint16_t manufacturer = 0;
  uint16_t device = 0;
  const bool present = i2cPresent(address);
  const bool idsRead =
      present && readRegister16(address, 0x3E, manufacturer) &&
      readRegister16(address, 0x3F, device);
  Serial.print("{\"address\":");
  Serial.print(address);
  Serial.print(",\"present\":");
  Serial.print(present ? "true" : "false");
  Serial.print(",\"ids_read\":");
  Serial.print(idsRead ? "true" : "false");
  Serial.print(",\"manufacturer_id\":");
  Serial.print(manufacturer);
  Serial.print(",\"device_id\":");
  Serial.print(device);
  Serial.print('}');
}

void runEnumeration() {
  Wire.begin();
  delay(20);

  uint8_t i2cAddresses[16] = {};
  size_t i2cCount = 0;
  for (uint8_t address = 1; address < 127; ++address) {
    if (i2cPresent(address) && i2cCount < sizeof(i2cAddresses)) {
      i2cAddresses[i2cCount++] = address;
    }
  }

  RS485.end();
  delay(20);
  RS485.setDelays(1000, 1125);
  RS485.begin(SOLINST_BAUD, SERIAL_8E1);
  RS485.receive();
  uint8_t solinstId = 0;
  uint16_t solinstValues[10] = {};
  for (uint8_t id = SOLINST_FIRST_ID; id <= SOLINST_LAST_ID; ++id) {
    if (probeSolinst(id, solinstValues)) {
      solinstId = id;
      break;
    }
  }

  const int wifiStatus = WiFi.status();
  const bool wifiModulePresent = wifiStatus != WL_NO_MODULE;

  Serial.print("{\"hil_protocol\":1,\"board\":\"opta\",\"wifi_module_present\":");
  Serial.print(wifiModulePresent ? "true" : "false");
  Serial.print(",\"wifi_status\":");
  Serial.print(wifiStatus);
  Serial.print(",\"i2c_addresses\":[");
  for (size_t i = 0; i < i2cCount; ++i) {
    if (i != 0) Serial.print(',');
    Serial.print(i2cAddresses[i]);
  }
  Serial.print("],\"oled_present\":");
  Serial.print(i2cPresent(OLED_ADDRESS) ? "true" : "false");
  Serial.print(",\"battery_ina228\":");
  printIna228(BATTERY_INA228_ADDRESS);
  Serial.print(",\"solar_ina228\":");
  printIna228(SOLAR_INA228_ADDRESS);
  Serial.print(",\"solinst_present\":");
  Serial.print(solinstId != 0 ? "true" : "false");
  Serial.print(",\"solinst_id\":");
  Serial.print(solinstId);
  Serial.print(",\"solinst_registers\":[");
  for (size_t i = 0; i < 10; ++i) {
    if (i != 0) Serial.print(',');
    Serial.print(solinstValues[i]);
  }
  Serial.println("],\"weather_expected\":false}");
}

}  // namespace

void setup() {
  Serial.begin(CONTROL_BAUD);
  const uint32_t startedMs = millis();
  while (!Serial && millis() - startedMs < 5000U) delay(10);
  Serial.println("{\"hil_protocol\":1,\"event\":\"ready\",\"board\":\"opta\"}");
}

void loop() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0 || value == '\r') continue;
    if (value == '\n') {
      command[commandLength] = '\0';
      if (strcmp(command, "RUN") == 0) runEnumeration();
      commandLength = 0;
    } else if (commandLength + 1U < sizeof(command)) {
      command[commandLength++] = static_cast<char>(value);
    } else {
      commandLength = 0;
    }
  }
  delay(1);
}
