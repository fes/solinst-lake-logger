#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SPI.h>

#include <gdeq0426t82_driver.h>
#include <logger_core/modbus_codec.h>
#include <sc16is752_spi.h>

#if defined(HIL_PLACEHOLDER_CONFIG)
#include "hil_config.example.h"
#elif __has_include("hil_config.h")
#include "hil_config.h"
#else
#error "Copy include/hil_config.example.h to include/hil_config.h and configure the reviewed Giga wiring"
#endif

namespace {

constexpr uint32_t CONTROL_BAUD = 115200;
constexpr size_t COMMAND_BYTES = 192;
constexpr size_t RESPONSE_BYTES = 96;
constexpr uint16_t MAX_HIL_REGISTERS = 32;
constexpr uint16_t EPAPER_PAGE_HEIGHT = 40;
constexpr uint16_t EPAPER_BLACK = GxEPD_BLACK;
constexpr uint16_t EPAPER_WHITE = GxEPD_WHITE;

char commandBuffer[COMMAND_BYTES];
size_t commandLength = 0;
bool discardUntilNewline = false;

struct Rs485Channel {
  uint8_t channel;
  bool enabled;
};

Rs485Channel channels[] = {
    {0, HIL_RS485_CHANNEL1_ENABLED != 0},
    {1, HIL_RS485_CHANNEL2_ENABLED != 0},
};
Sc16is752Spi rs485Bridge(
    SPI1, HIL_RS485_CS_PIN, HIL_RS485_IRQ_PIN,
    HIL_RS485_CHANNEL1_ENABLE_PIN, HIL_RS485_CHANNEL2_ENABLE_PIN,
    HIL_RS485_TRANSMIT_ENABLE_LEVEL);
bool rs485BridgePresent = false;
GxEPD2_BW<Gdeq0426t82Driver, EPAPER_PAGE_HEIGHT> epaper(
    Gdeq0426t82Driver(
        HIL_EPAPER_CS_PIN, HIL_EPAPER_DC_PIN, HIL_EPAPER_RST_PIN,
        HIL_EPAPER_BUSY_PIN));
bool epaperInitialized = false;
bool partialPatternBlack = false;

void printJsonString(const char* value) {
  Serial.print('"');
  for (const char* cursor = value; *cursor != '\0'; ++cursor) {
    if (*cursor == '"' || *cursor == '\\') Serial.print('\\');
    if (static_cast<uint8_t>(*cursor) >= 0x20U) Serial.print(*cursor);
  }
  Serial.print('"');
}

void beginResponse(const char* command, bool ok) {
  Serial.print("{\"hil_protocol\":1,\"command\":");
  printJsonString(command);
  Serial.print(",\"ok\":");
  Serial.print(ok ? "true" : "false");
}

void printError(const char* command, const char* error) {
  beginResponse(command, false);
  Serial.print(",\"error\":");
  printJsonString(error);
  Serial.println("}");
}

bool parseUnsigned(const char* text, uint32_t minimum, uint32_t maximum,
                   uint32_t& value) {
  if (text == nullptr || *text == '\0' || *text == '-') return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 0);
  if (*end != '\0' || parsed < minimum || parsed > maximum) return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseFraming(const char* text, Sc16is752Spi::LineFormat& framing) {
  if (text == nullptr) return false;
  if (strcmp(text, "8N1") == 0) {
    framing = Sc16is752Spi::LineFormat::EIGHT_N_ONE;
    return true;
  }
  if (strcmp(text, "8E1") == 0) {
    framing = Sc16is752Spi::LineFormat::EIGHT_E_ONE;
    return true;
  }
  return false;
}

void printHex(const uint8_t* data, size_t length) {
  static const char digits[] = "0123456789ABCDEF";
  Serial.print('"');
  for (size_t i = 0; i < length; ++i) {
    Serial.print(digits[data[i] >> 4U]);
    Serial.print(digits[data[i] & 0x0FU]);
  }
  Serial.print('"');
}

void handleHello() {
  beginResponse("HELLO", true);
  Serial.print(",\"device\":");
  printJsonString(HIL_DEVICE_NAME);
  Serial.print(",\"board\":\"giga_r1_m7\",\"capabilities\":{");
  Serial.print("\"rs485_channel_1\":");
  Serial.print(channels[0].enabled ? "true" : "false");
  Serial.print(",\"rs485_channel_2\":");
  Serial.print(channels[1].enabled ? "true" : "false");
  Serial.print(",\"rs485_bridge\":");
  Serial.print(rs485BridgePresent ? "true" : "false");
  Serial.print(",\"epaper\":");
  Serial.print(HIL_EPAPER_ENABLED ? "true" : "false");
  Serial.println("}}");
}

void handleStatus() {
  beginResponse("STATUS", true);
  Serial.print(",\"uptime_ms\":");
  Serial.print(millis());
  Serial.print(",\"rs485_irq_active\":");
  if (!rs485BridgePresent) {
    Serial.print("null");
  } else {
    Serial.print(rs485Bridge.interruptActive() ? "true" : "false");
  }
  Serial.print(",\"rs485_channels\":[");
  for (uint8_t channel = 0; channel < 2; ++channel) {
    if (channel != 0) Serial.print(',');
    Serial.print("{\"tx_space\":");
    Serial.print(rs485BridgePresent
                     ? rs485Bridge.transmitAvailable(channel)
                     : 0);
    Serial.print(",\"rx_count\":");
    Serial.print(rs485BridgePresent ? rs485Bridge.available(channel) : 0);
    Serial.print(",\"line_status\":");
    Serial.print(
        rs485BridgePresent ? rs485Bridge.lineStatus(channel) : 0);
    Serial.print('}');
  }
  Serial.print(']');
  Serial.print(",\"epaper_busy\":");
  if (!HIL_EPAPER_ENABLED || HIL_EPAPER_BUSY_PIN < 0) {
    Serial.print("null");
  } else {
    Serial.print(digitalRead(HIL_EPAPER_BUSY_PIN) == HIL_EPAPER_BUSY_LEVEL
                     ? "true"
                     : "false");
  }
  Serial.print(",\"epaper_power_enabled\":");
  if (!HIL_EPAPER_ENABLED || HIL_EPAPER_POWER_PIN < 0) {
    Serial.print("null");
  } else {
    Serial.print(
        digitalRead(HIL_EPAPER_POWER_PIN) ==
                HIL_EPAPER_POWER_ENABLE_LEVEL
            ? "true"
            : "false");
  }
  Serial.println("}");
}

void handleRs485Read(char* save) {
  const char* command = "RS485_READ";
  char* channelText = strtok_r(nullptr, " ", &save);
  char* baudText = strtok_r(nullptr, " ", &save);
  char* framingText = strtok_r(nullptr, " ", &save);
  char* slaveText = strtok_r(nullptr, " ", &save);
  char* functionText = strtok_r(nullptr, " ", &save);
  char* registerText = strtok_r(nullptr, " ", &save);
  char* quantityText = strtok_r(nullptr, " ", &save);
  char* timeoutText = strtok_r(nullptr, " ", &save);
  if (timeoutText == nullptr || strtok_r(nullptr, " ", &save) != nullptr) {
    printError(command, "usage: RS485_READ channel baud 8N1|8E1 slave function start quantity timeout_ms");
    return;
  }

  uint32_t channelNumber = 0;
  uint32_t baud = 0;
  uint32_t slave = 0;
  uint32_t function = 0;
  uint32_t startRegister = 0;
  uint32_t quantity = 0;
  uint32_t timeoutMs = 0;
  Sc16is752Spi::LineFormat framing =
      Sc16is752Spi::LineFormat::EIGHT_N_ONE;
  if (!parseUnsigned(channelText, 1, 2, channelNumber) ||
      !parseUnsigned(baudText, 300, 1000000, baud) ||
      !parseFraming(framingText, framing) ||
      !parseUnsigned(slaveText, 1, 247, slave) ||
      !parseUnsigned(functionText, 3, 4, function) ||
      (function != 3 && function != 4) ||
      !parseUnsigned(registerText, 0, UINT16_MAX, startRegister) ||
      !parseUnsigned(quantityText, 1, MAX_HIL_REGISTERS, quantity) ||
      !parseUnsigned(timeoutText, 10, 10000, timeoutMs)) {
    printError(command, "invalid RS-485 read argument");
    return;
  }

  Rs485Channel& channel = channels[channelNumber - 1U];
  if (!channel.enabled) {
    printError(command, "channel disabled in hil_config.h");
    return;
  }
  rs485BridgePresent =
      rs485Bridge.configure(channel.channel, baud, framing);
  if (!rs485BridgePresent) {
    printError(command, "SC16IS752 bridge unavailable");
    return;
  }

  uint8_t request[logger_core::MODBUS_READ_REQUEST_BYTES];
  if (logger_core::buildModbusReadRequest(
          static_cast<uint8_t>(slave), static_cast<uint8_t>(function),
          static_cast<uint16_t>(startRegister),
          static_cast<uint16_t>(quantity), request, sizeof(request)) == 0) {
    printError(command, "could not encode Modbus request");
    return;
  }

  rs485Bridge.clearReceive(channel.channel, 20U, 100U);
  if (!rs485Bridge.write(
          channel.channel, request, sizeof(request), 1000U, 500U)) {
    printError(command, "SC16IS752 transmit failed");
    return;
  }

  uint8_t response[RESPONSE_BYTES] = {};
  size_t responseLength = 0;
  const uint32_t startedMs = millis();
  uint16_t values[MAX_HIL_REGISTERS] = {};
  size_t frameOffset = 0;
  logger_core::ModbusDecodeResult decode =
      logger_core::ModbusDecodeResult::FRAME_NOT_FOUND;
  while (millis() - startedMs < timeoutMs) {
    while (rs485Bridge.available(channel.channel) > 0) {
      const int value = rs485Bridge.read(channel.channel);
      if (value >= 0 && responseLength < sizeof(response)) {
        response[responseLength++] = static_cast<uint8_t>(value);
      }
    }
    decode = logger_core::decodeModbusReadRegisters(
        response, responseLength, static_cast<uint8_t>(slave),
        static_cast<uint8_t>(function), static_cast<uint16_t>(quantity), values,
        MAX_HIL_REGISTERS, &frameOffset);
    if (decode == logger_core::ModbusDecodeResult::OK) break;
    delay(1);
  }

  const bool ok = decode == logger_core::ModbusDecodeResult::OK;
  beginResponse(command, ok);
  Serial.print(",\"channel\":");
  Serial.print(channelNumber);
  Serial.print(",\"elapsed_ms\":");
  Serial.print(millis() - startedMs);
  Serial.print(",\"raw_hex\":");
  printHex(response, responseLength);
  if (ok) {
    Serial.print(",\"frame_offset\":");
    Serial.print(frameOffset);
    Serial.print(",\"registers\":[");
    for (size_t i = 0; i < quantity; ++i) {
      if (i != 0) Serial.print(',');
      Serial.print(values[i]);
    }
    Serial.println("]}");
  } else {
    Serial.println(",\"error\":\"no valid Modbus response\"}");
  }
}

void handleRs485Loopback(char* save) {
  const char* command = "RS485_LOOPBACK";
  char* channelText = strtok_r(nullptr, " ", &save);
  uint32_t channelNumber = 0;
  if (channelText == nullptr || strtok_r(nullptr, " ", &save) != nullptr ||
      !parseUnsigned(channelText, 1, 2, channelNumber)) {
    printError(command, "usage: RS485_LOOPBACK channel");
    return;
  }

  Rs485Channel& channel = channels[channelNumber - 1U];
  if (!channel.enabled) {
    printError(command, "channel disabled in hil_config.h");
    return;
  }
  if (!rs485Bridge.configure(
          channel.channel, 19200,
          Sc16is752Spi::LineFormat::EIGHT_N_ONE)) {
    printError(command, "SC16IS752 bridge unavailable");
    return;
  }

  const bool ok = rs485Bridge.internalLoopbackTest(channel.channel, 100U);
  beginResponse(command, ok);
  Serial.print(",\"channel\":");
  Serial.print(channelNumber);
  if (!ok) Serial.print(",\"error\":\"internal UART loopback failed\"");
  Serial.println("}");
}

void handleEpaperWait(char* save) {
  const char* command = "EPAPER_WAIT_IDLE";
  char* timeoutText = strtok_r(nullptr, " ", &save);
  uint32_t timeoutMs = 0;
  if (timeoutText == nullptr || strtok_r(nullptr, " ", &save) != nullptr ||
      !parseUnsigned(timeoutText, 1, 120000, timeoutMs)) {
    printError(command, "usage: EPAPER_WAIT_IDLE timeout_ms");
    return;
  }
  if (!HIL_EPAPER_ENABLED || HIL_EPAPER_BUSY_PIN < 0) {
    printError(command, "e-paper busy input disabled in hil_config.h");
    return;
  }

  const uint32_t startedMs = millis();
  while (digitalRead(HIL_EPAPER_BUSY_PIN) == HIL_EPAPER_BUSY_LEVEL &&
         millis() - startedMs < timeoutMs) {
    delay(1);
  }
  const bool idle =
      digitalRead(HIL_EPAPER_BUSY_PIN) != HIL_EPAPER_BUSY_LEVEL;
  beginResponse(command, idle);
  Serial.print(",\"idle\":");
  Serial.print(idle ? "true" : "false");
  Serial.print(",\"elapsed_ms\":");
  Serial.print(millis() - startedMs);
  if (!idle) Serial.print(",\"error\":\"e-paper busy timeout\"");
  Serial.println("}");
}

void handleEpaperReset(char* save) {
  const char* command = "EPAPER_RESET";
  char* confirmation = strtok_r(nullptr, " ", &save);
  if (confirmation == nullptr || strcmp(confirmation, "CONFIRM") != 0 ||
      strtok_r(nullptr, " ", &save) != nullptr) {
    printError(command, "explicit CONFIRM token required");
    return;
  }
  if (!HIL_EPAPER_ENABLED || HIL_EPAPER_RST_PIN < 0) {
    printError(command, "e-paper reset output disabled in hil_config.h");
    return;
  }
  digitalWrite(HIL_EPAPER_RST_PIN, LOW);
  delay(20);
  digitalWrite(HIL_EPAPER_RST_PIN, HIGH);
  delay(20);
  beginResponse(command, true);
  Serial.println(",\"reset_pulsed\":true}");
}

void handleEpaperPattern(char* save) {
  const char* command = "EPAPER_PATTERN";
  char* confirmation = strtok_r(nullptr, " ", &save);
  if (confirmation == nullptr || strcmp(confirmation, "CONFIRM") != 0 ||
      strtok_r(nullptr, " ", &save) != nullptr) {
    printError(command, "explicit CONFIRM token required");
    return;
  }
  if (!HIL_EPAPER_ENABLED) {
    printError(command, "e-paper disabled in hil_config.h");
    return;
  }

  const uint32_t startedMs = millis();
  if (!epaperInitialized) {
    epaper.epd2.selectSPI(
        SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    epaper.init(0, true, 10, false);
    epaper.setRotation(0);
    epaper.setTextWrap(false);
    epaperInitialized = true;
  }

  epaper.setFullWindow();
  epaper.firstPage();
  do {
    epaper.fillScreen(EPAPER_BLACK);
    epaper.setTextColor(EPAPER_WHITE);
    epaper.setTextSize(5);
    epaper.setCursor(150, 250);
    epaper.print("GIGA EPAPER TEST");
  } while (epaper.nextPage());
  epaper.powerOff();

  const bool idle =
      digitalRead(HIL_EPAPER_BUSY_PIN) != HIL_EPAPER_BUSY_LEVEL;
  beginResponse(command, idle);
  Serial.print(",\"idle\":");
  Serial.print(idle ? "true" : "false");
  Serial.print(",\"elapsed_ms\":");
  Serial.print(millis() - startedMs);
  if (!idle) Serial.print(",\"error\":\"e-paper busy after refresh\"");
  Serial.println("}");
}

void handleEpaperPartialPattern(char* save) {
  const char* command = "EPAPER_PARTIAL_PATTERN";
  char* confirmation = strtok_r(nullptr, " ", &save);
  if (confirmation == nullptr || strcmp(confirmation, "CONFIRM") != 0 ||
      strtok_r(nullptr, " ", &save) != nullptr) {
    printError(command, "explicit CONFIRM token required");
    return;
  }
  if (!HIL_EPAPER_ENABLED || !epaperInitialized) {
    printError(command, "run EPAPER_PATTERN CONFIRM first");
    return;
  }

  partialPatternBlack = !partialPatternBlack;
  const uint32_t startedMs = millis();
  epaper.setPartialWindow(0, 0, epaper.width(), epaper.height());
  epaper.firstPage();
  do {
    epaper.fillScreen(partialPatternBlack ? EPAPER_BLACK : EPAPER_WHITE);
    epaper.setTextColor(
        partialPatternBlack ? EPAPER_WHITE : EPAPER_BLACK);
    epaper.setTextSize(5);
    epaper.setCursor(110, 250);
    epaper.print("PARTIAL REFRESH TEST");
  } while (epaper.nextPage());
  epaper.powerOff();

  const bool idle =
      digitalRead(HIL_EPAPER_BUSY_PIN) != HIL_EPAPER_BUSY_LEVEL;
  beginResponse(command, idle);
  Serial.print(",\"idle\":");
  Serial.print(idle ? "true" : "false");
  Serial.print(",\"elapsed_ms\":");
  Serial.print(millis() - startedMs);
  if (!idle) Serial.print(",\"error\":\"e-paper busy after partial refresh\"");
  Serial.println("}");
}

void handleCommand(char* line) {
  char* save = nullptr;
  char* command = strtok_r(line, " ", &save);
  if (command == nullptr) return;
  if (strcmp(command, "HELLO") == 0 && strtok_r(nullptr, " ", &save) == nullptr) {
    handleHello();
  } else if (strcmp(command, "STATUS") == 0 &&
             strtok_r(nullptr, " ", &save) == nullptr) {
    handleStatus();
  } else if (strcmp(command, "RS485_READ") == 0) {
    handleRs485Read(save);
  } else if (strcmp(command, "RS485_LOOPBACK") == 0) {
    handleRs485Loopback(save);
  } else if (strcmp(command, "EPAPER_WAIT_IDLE") == 0) {
    handleEpaperWait(save);
  } else if (strcmp(command, "EPAPER_RESET") == 0) {
    handleEpaperReset(save);
  } else if (strcmp(command, "EPAPER_PATTERN") == 0) {
    handleEpaperPattern(save);
  } else if (strcmp(command, "EPAPER_PARTIAL_PATTERN") == 0) {
    handleEpaperPartialPattern(save);
  } else {
    printError(command, "unknown or malformed command");
  }
}

void configureOutputPin(int pin, uint8_t inactiveLevel) {
  if (pin < 0) return;
  digitalWrite(pin, inactiveLevel);
  pinMode(pin, OUTPUT);
}

}  // namespace

void setup() {
  Serial.begin(CONTROL_BAUD);
  const uint32_t waitStartedMs = millis();
  while (!Serial && millis() - waitStartedMs < 5000U) delay(10);

  configureOutputPin(HIL_RS485_CS_PIN, HIGH);
  configureOutputPin(HIL_EPAPER_CS_PIN, HIGH);
  const uint8_t rs485ReceiveLevel =
      HIL_RS485_TRANSMIT_ENABLE_LEVEL == HIGH ? LOW : HIGH;
  configureOutputPin(
      HIL_RS485_CHANNEL1_ENABLE_PIN, rs485ReceiveLevel);
  configureOutputPin(
      HIL_RS485_CHANNEL2_ENABLE_PIN, rs485ReceiveLevel);
  if (channels[0].enabled || channels[1].enabled) {
    rs485BridgePresent = rs485Bridge.begin();
  }
  if (HIL_EPAPER_ENABLED) {
    configureOutputPin(HIL_EPAPER_DC_PIN, LOW);
    configureOutputPin(HIL_EPAPER_RST_PIN, HIGH);
    configureOutputPin(
        HIL_EPAPER_POWER_PIN,
        HIL_EPAPER_POWER_ENABLE_LEVEL == HIGH ? LOW : HIGH);
    delay(100);
    digitalWrite(
        HIL_EPAPER_POWER_PIN, HIL_EPAPER_POWER_ENABLE_LEVEL);
    delay(100);
    if (HIL_EPAPER_BUSY_PIN >= 0) {
      pinMode(HIL_EPAPER_BUSY_PIN, INPUT_PULLDOWN);
    }
  } else {
    configureOutputPin(
        HIL_EPAPER_POWER_PIN,
        HIL_EPAPER_POWER_ENABLE_LEVEL == HIGH ? LOW : HIGH);
  }

  Serial.println("{\"hil_protocol\":1,\"event\":\"ready\"}");
}

void loop() {
  while (Serial.available() > 0) {
    const int value = Serial.read();
    if (value < 0) continue;
    const char byte = static_cast<char>(value);
    if (byte == '\r') continue;
    if (byte == '\n') {
      if (discardUntilNewline) {
        discardUntilNewline = false;
        commandLength = 0;
        continue;
      }
      commandBuffer[commandLength] = '\0';
      handleCommand(commandBuffer);
      commandLength = 0;
      continue;
    }
    if (discardUntilNewline) continue;
    if (commandLength + 1U >= sizeof(commandBuffer)) {
      commandLength = 0;
      discardUntilNewline = true;
      printError("INPUT", "command too long");
      continue;
    }
    if (static_cast<uint8_t>(byte) >= 0x20U &&
        static_cast<uint8_t>(byte) <= 0x7EU) {
      commandBuffer[commandLength++] = byte;
    }
  }
  delay(1);
}
