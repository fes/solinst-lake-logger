#include <InkplateMotion.h>

#include "inkplate_protocol.h"

using inkplate_protocol::DisplaySnapshot;
using inkplate_protocol::Frame;
using inkplate_protocol::FrameType;
using inkplate_protocol::ParseResult;

namespace {

constexpr uint32_t DEVICE_BAUD = 115200;
constexpr uint32_t USB_BAUD = 115200;
constexpr uint16_t FULL_REFRESH_THRESHOLD = 40;
constexpr uint32_t REBOOT_DELAY_MS = 100;

Inkplate inkplate;
HardwareSerial deviceSerial(PB11, PB10);  // RX, TX; USART3.

DisplaySnapshot snapshot;
bool snapshotAvailable = false;
bool renderingPaused = false;
bool displayBlank = false;
uint32_t acceptedFrames = 0;
uint32_t rejectedFrames = 0;
uint32_t lastSnapshotMs = 0;
char lastCommand[16] = "boot";

struct LineReader {
  char buffer[inkplate_protocol::MAX_FRAME_LENGTH + 1] = {};
  size_t length = 0;
  bool overflow = false;
  uint32_t lastSequence = 0;
};

LineReader deviceReader;
LineReader usbReader;

void printAt(int16_t x, int16_t y, uint8_t size, const char* text) {
  inkplate.setTextSize(size);
  inkplate.setCursor(x, y);
  inkplate.print(text);
}

void printRight(int16_t right, int16_t y, uint8_t size, const char* text) {
  const int16_t width = static_cast<int16_t>(strlen(text) * 6U * size);
  printAt(right - width, y, size, text);
}

void drawCard(
    int16_t x, int16_t y, int16_t width, int16_t height,
    const char* title) {
  inkplate.drawRoundRect(x, y, width, height, 10, BLACK);
  inkplate.fillRoundRect(x + 1, y + 1, width - 2, 38, 9, BLACK);
  inkplate.setTextColor(WHITE);
  printAt(x + 14, y + 11, 2, title);
  inkplate.setTextColor(BLACK);
}

void formatAge(int32_t seconds, char* output, size_t length) {
  if (seconds < 0) {
    snprintf(output, length, "never");
  } else if (seconds >= 3600) {
    snprintf(
        output, length, "%ldh %ldm", static_cast<long>(seconds / 3600),
        static_cast<long>((seconds % 3600) / 60));
  } else if (seconds >= 60) {
    snprintf(output, length, "%ldm", static_cast<long>(seconds / 60));
  } else {
    snprintf(output, length, "%lds", static_cast<long>(seconds));
  }
}

void formatFloat(float value, uint8_t decimalPlaces, char* output) {
  // The default Inkplate Newlib Nano runtime omits floating-point printf.
  dtostrf(value, 1, decimalPlaces, output);
}

void drawDashboard() {
  char line[96];
  char age[24];
  char value1[48];
  char value2[48];
  const int16_t width = inkplate.width();
  const int16_t margin = 16;
  const int16_t gap = 14;
  const int16_t headerHeight = 62;
  const int16_t topY = headerHeight + 10;
  const int16_t topHeight = 292;
  const int16_t lowerY = topY + topHeight + gap;
  const int16_t footerY = 718;
  const int16_t lowerHeight = footerY - lowerY - 10;
  const int16_t halfWidth = (width - margin * 2 - gap) / 2;
  const int16_t thirdWidth = (width - margin * 2 - gap * 2) / 3;

  inkplate.clearDisplay();
  inkplate.fillRect(0, 0, width, headerHeight, BLACK);
  inkplate.setTextColor(WHITE);
  printAt(18, 18, 3, snapshot.device);
  snprintf(line, sizeof(line), "%s  %s", snapshot.health, snapshot.timestampUtc);
  printRight(width - 18, 18, 2, line);
  inkplate.setTextColor(BLACK);

  drawCard(margin, topY, halfWidth, topHeight, "WATER");
  if (snapshot.waterValid) {
    formatFloat(snapshot.waterLevelM, 3, value1);
    snprintf(line, sizeof(line), "%s m", value1);
    printAt(margin + 24, topY + 88, 6, line);
    formatFloat(snapshot.waterTemperatureC, 2, value1);
    snprintf(line, sizeof(line), "%s C", value1);
    printAt(margin + 28, topY + 166, 4, line);
  } else {
    printAt(margin + 24, topY + 105, 5, "UNAVAILABLE");
  }
  formatAge(snapshot.probeAgeSeconds, age, sizeof(age));
  snprintf(line, sizeof(line), "Probe age %s", age);
  printAt(margin + 24, topY + 225, 2, line);
  snprintf(
      line, sizeof(line), "Sensor %s | ID %lu | SN %lu",
      snapshot.sensorFound ? "FOUND" : "MISSING",
      static_cast<unsigned long>(snapshot.sensorModbusId),
      static_cast<unsigned long>(snapshot.sensorSerialNumber));
  printAt(margin + 24, topY + 258, 1, line);

  const int16_t weatherX = margin + halfWidth + gap;
  drawCard(weatherX, topY, halfWidth, topHeight, "WEATHER");
  if (snapshot.weatherValid) {
    formatFloat(snapshot.airTemperatureC, 1, value1);
    formatFloat(snapshot.relativeHumidityPct, 0, value2);
    snprintf(
        line, sizeof(line), "%s C     %s%% RH", value1, value2);
    printAt(weatherX + 24, topY + 76, 4, line);
    formatFloat(snapshot.barometricPressureHpa, 0, value1);
    snprintf(line, sizeof(line), "%s hPa", value1);
    printAt(weatherX + 24, topY + 132, 3, line);
    formatFloat(snapshot.windSpeedMs, 1, value1);
    formatFloat(snapshot.windDirectionDeg, 0, value2);
    snprintf(
        line, sizeof(line), "Wind %s m/s @ %s deg", value1, value2);
    printAt(weatherX + 24, topY + 182, 3, line);
    if (snapshot.rainfallValid) {
      formatFloat(snapshot.rainfallIntervalMm, 1, value1);
      snprintf(line, sizeof(line), "Interval rain %s mm", value1);
    } else {
      snprintf(line, sizeof(line), "Interval rain --");
    }
    printAt(weatherX + 24, topY + 233, 2, line);
  } else {
    printAt(
        weatherX + 24, topY + 112, 4,
        snapshot.weatherEnabled ? "NO WEATHER DATA" : "DISABLED");
    snprintf(
        line, sizeof(line), "Station %s",
        snapshot.weatherPresent ? "present" : "not detected");
    printAt(weatherX + 24, topY + 175, 2, line);
  }

  drawCard(margin, lowerY, thirdWidth, lowerHeight, "POWER");
  if (snapshot.batteryValid) {
    formatFloat(snapshot.batteryVoltageV, 2, value1);
    formatFloat(snapshot.batteryChargePct, 0, value2);
    snprintf(
        line, sizeof(line), "%s V   %s%%", value1, value2);
    printAt(margin + 20, lowerY + 72, 3, line);
  } else {
    printAt(margin + 20, lowerY + 78, 3, "BATTERY --");
  }
  if (snapshot.batteryExtremaValid) {
    formatFloat(snapshot.batteryVoltageMin24hV, 2, value1);
    formatFloat(snapshot.batteryVoltageMax24hV, 2, value2);
    snprintf(
        line, sizeof(line), "24h %s - %s V", value1, value2);
    printAt(margin + 20, lowerY + 120, 2, line);
  }
  if (snapshot.solarValid) {
    formatFloat(snapshot.solarVoltageV, 2, value1);
    snprintf(line, sizeof(line), "Solar %s V", value1);
    printAt(margin + 20, lowerY + 168, 2, line);
    formatFloat(snapshot.solarCurrentA, 2, value1);
    formatFloat(snapshot.solarPowerW, 1, value2);
    snprintf(
        line, sizeof(line), "%s A   %s W", value1, value2);
    printAt(margin + 20, lowerY + 204, 2, line);
    printAt(
        margin + 20, lowerY + 240, 2,
        snapshot.solarCharging ? "CHARGING" : "IDLE");
  } else {
    printAt(margin + 20, lowerY + 180, 2, "SOLAR --");
  }

  const int16_t loggerX = margin + thirdWidth + gap;
  drawCard(loggerX, lowerY, thirdWidth, lowerHeight, "LOGGER");
  snprintf(
      line, sizeof(line), "Clock  %s",
      snapshot.clockValid ? "SYNCED" : "INVALID");
  printAt(loggerX + 20, lowerY + 72, 3, line);
  formatAge(snapshot.uploadAgeSeconds, age, sizeof(age));
  snprintf(line, sizeof(line), "Upload age  %s", age);
  printAt(loggerX + 20, lowerY + 124, 2, line);
  snprintf(
      line, sizeof(line), "Backlog  %lu",
      static_cast<unsigned long>(snapshot.backlogCount));
  printAt(loggerX + 20, lowerY + 166, 2, line);
  snprintf(
      line, sizeof(line), "Failures  %lu",
      static_cast<unsigned long>(snapshot.consecutiveUploadFailures));
  printAt(loggerX + 20, lowerY + 208, 2, line);
  if (strcmp(snapshot.health, "HEALTHY") != 0) {
    inkplate.fillRoundRect(
        loggerX + 18, lowerY + 244, thirdWidth - 36, 36, 5, BLACK);
    inkplate.setTextColor(WHITE);
    printAt(loggerX + 34, lowerY + 255, 2, "ATTENTION REQUIRED");
    inkplate.setTextColor(BLACK);
  }

  const int16_t commX = loggerX + thirdWidth + gap;
  drawCard(commX, lowerY, thirdWidth, lowerHeight, "NETWORK / SERIAL");
  snprintf(
      line, sizeof(line), "WiFi  %s  %ld dBm",
      snapshot.wifiConnected ? "ON" : "OFF",
      static_cast<long>(snapshot.wifiRssiDbm));
  printAt(commX + 20, lowerY + 72, 2, line);
  snprintf(line, sizeof(line), "IP  %s", snapshot.ipAddress);
  printAt(commX + 20, lowerY + 112, 2, line);
  snprintf(
      line, sizeof(line), "Frames  %lu ok / %lu bad",
      static_cast<unsigned long>(acceptedFrames),
      static_cast<unsigned long>(rejectedFrames));
  printAt(commX + 20, lowerY + 164, 2, line);
  snprintf(
      line, sizeof(line), "Sequence  %lu",
      static_cast<unsigned long>(deviceReader.lastSequence));
  printAt(commX + 20, lowerY + 204, 2, line);
  snprintf(line, sizeof(line), "Last command  %s", lastCommand);
  printAt(commX + 20, lowerY + 244, 2, line);

  inkplate.drawFastHLine(margin, footerY, width - margin * 2, BLACK);
  snprintf(
      line, sizeof(line),
      "Inkplate UART display v1 | 115200 8N1 | %s | snapshot received %lus ago",
      renderingPaused ? "PAUSED" : "ACTIVE",
      snapshotAvailable
          ? static_cast<unsigned long>((millis() - lastSnapshotMs) / 1000UL)
          : 0UL);
  printAt(margin, footerY + 14, 2, line);
}

void render(bool fullRefresh, bool force = false) {
  if (!snapshotAvailable || (renderingPaused && !force)) return;
  drawDashboard();
  if (fullRefresh) {
    inkplate.display();
  } else {
    inkplate.partialUpdate();
  }
  displayBlank = false;
}

template <typename SerialPort>
void sendResponse(
    SerialPort& serial, uint32_t sequence, const char* type,
    const char* payload) {
  char body[384];
  const int bodyLength = snprintf(
      body, sizeof(body), "%u|%lu|%s|%s",
      static_cast<unsigned>(inkplate_protocol::VERSION),
      static_cast<unsigned long>(sequence), type, payload);
  if (bodyLength <= 0 || static_cast<size_t>(bodyLength) >= sizeof(body)) return;
  const uint16_t crc =
      inkplate_protocol::crc16Ccitt(body, static_cast<size_t>(bodyLength));
  serial.print('@');
  serial.print(body);
  serial.print('*');
  char checksum[5];
  snprintf(checksum, sizeof(checksum), "%04X", crc);
  serial.println(checksum);
}

template <typename SerialPort>
void sendStatus(
    SerialPort& serial, uint32_t sequence, uint32_t sourceLastSequence) {
  char status[300];
  snprintf(
      status, sizeof(status),
      "state=%s;snapshot=%u;blank=%u;accepted=%lu;rejected=%lu;"
      "last_sequence=%lu;uptime_s=%lu",
      renderingPaused ? "paused" : "active", snapshotAvailable ? 1U : 0U,
      displayBlank ? 1U : 0U, static_cast<unsigned long>(acceptedFrames),
      static_cast<unsigned long>(rejectedFrames),
      static_cast<unsigned long>(sourceLastSequence),
      static_cast<unsigned long>(millis() / 1000UL));
  sendResponse(serial, sequence, "ACK", status);
}

void copyCommand(const char* command) {
  snprintf(lastCommand, sizeof(lastCommand), "%.15s", command);
}

template <typename SerialPort>
void handleCommand(
    SerialPort& serial, const Frame& frame, uint32_t sourceLastSequence) {
  copyCommand(frame.payload);
  if (strcmp(frame.payload, "status") == 0) {
    sendStatus(serial, frame.sequence, sourceLastSequence);
  } else if (strcmp(frame.payload, "help") == 0) {
    sendResponse(
        serial, frame.sequence, "ACK",
        "commands=status,help,refresh,clear,pause,resume,reboot,sleep");
  } else if (strcmp(frame.payload, "refresh") == 0) {
    render(true, true);
    sendResponse(serial, frame.sequence, "ACK", "command=refresh");
  } else if (strcmp(frame.payload, "clear") == 0) {
    renderingPaused = true;
    inkplate.clearDisplay();
    inkplate.display();
    displayBlank = true;
    sendResponse(serial, frame.sequence, "ACK", "command=clear;state=paused");
  } else if (strcmp(frame.payload, "pause") == 0) {
    renderingPaused = true;
    sendResponse(serial, frame.sequence, "ACK", "command=pause");
  } else if (strcmp(frame.payload, "resume") == 0) {
    renderingPaused = false;
    render(true);
    sendResponse(serial, frame.sequence, "ACK", "command=resume");
  } else if (strcmp(frame.payload, "reboot") == 0) {
    sendResponse(serial, frame.sequence, "ACK", "command=reboot");
    serial.flush();
    delay(REBOOT_DELAY_MS);
    NVIC_SystemReset();
  } else if (strcmp(frame.payload, "sleep") == 0) {
    sendResponse(
        serial, frame.sequence, "ACK",
        "command=sleep;wake=button_or_reset");
    serial.flush();
    delay(REBOOT_DELAY_MS);
    inkplate.peripheralState(INKPLATE_PERIPHERAL_ALL, false);
    HAL_PWREx_DisableUSBVoltageDetector();
    HAL_PWREx_ControlStopModeVoltageScaling(PWR_REGULATOR_SVOS_SCALE5);
    HAL_PWREx_EnterSTANDBYMode(PWR_D3_DOMAIN);
    HAL_PWREx_EnterSTANDBYMode(PWR_D2_DOMAIN);
    HAL_PWREx_EnterSTANDBYMode(PWR_D1_DOMAIN);
  } else {
    sendResponse(serial, frame.sequence, "ERROR", "reason=unknown_command");
  }
}

template <typename SerialPort>
void processLine(
    SerialPort& serial, const char* line, uint32_t& sourceLastSequence) {
  Frame frame;
  const ParseResult result = inkplate_protocol::parseFrame(line, frame);
  if (result != ParseResult::OK) {
    ++rejectedFrames;
    char error[64];
    snprintf(
        error, sizeof(error), "reason=%s",
        inkplate_protocol::parseResultName(result));
    sendResponse(serial, 0, "ERROR", error);
    return;
  }
  if (sourceLastSequence != 0 && frame.sequence <= sourceLastSequence) {
    ++rejectedFrames;
    char response[80];
    snprintf(
        response, sizeof(response), "reason=stale_sequence;last_sequence=%lu",
        static_cast<unsigned long>(sourceLastSequence));
    sendResponse(serial, frame.sequence, "ERROR", response);
    return;
  }
  sourceLastSequence = frame.sequence;
  ++acceptedFrames;
  if (frame.type == FrameType::COMMAND) {
    handleCommand(serial, frame, sourceLastSequence);
    return;
  }
  if (frame.type != FrameType::SNAPSHOT) {
    ++rejectedFrames;
    --acceptedFrames;
    sendResponse(serial, frame.sequence, "ERROR", "reason=unexpected_type");
    return;
  }

  DisplaySnapshot candidate;
  const char* error = nullptr;
  if (!inkplate_protocol::parseSnapshot(frame.payload, candidate, error)) {
    ++rejectedFrames;
    --acceptedFrames;
    char response[80];
    snprintf(response, sizeof(response), "reason=%s", error);
    sendResponse(serial, frame.sequence, "ERROR", response);
    return;
  }
  snapshot = candidate;
  snapshotAvailable = true;
  lastSnapshotMs = millis();
  render(false);
  sendResponse(serial, frame.sequence, "ACK", "snapshot=accepted");
}

template <typename SerialPort>
void consumeSerial(SerialPort& serial, LineReader& reader) {
  while (serial.available() > 0) {
    const int incoming = serial.read();
    if (incoming < 0) return;
    const char value = static_cast<char>(incoming);
    if (value == '\r') continue;
    if (value == '\n') {
      if (reader.overflow) {
        ++rejectedFrames;
        sendResponse(serial, 0, "ERROR", "reason=line_too_long");
      } else if (reader.length > 0) {
        reader.buffer[reader.length] = '\0';
        processLine(serial, reader.buffer, reader.lastSequence);
      }
      reader.length = 0;
      reader.overflow = false;
      continue;
    }
    if (reader.overflow) continue;
    if (reader.length >= inkplate_protocol::MAX_FRAME_LENGTH) {
      reader.overflow = true;
      continue;
    }
    reader.buffer[reader.length++] = value;
  }
}

void drawWaitingScreen() {
  inkplate.clearDisplay();
  inkplate.setTextColor(BLACK);
  printAt(44, 80, 5, "Lake Logger Display");
  printAt(48, 160, 3, "Waiting for a snapshot from the GIGA");
  printAt(48, 230, 2, "UART: USART3 PB11 RX / PB10 TX, 115200 8N1");
  printAt(48, 275, 2, "USB Serial accepts the same framed protocol for setup.");
  printAt(48, 350, 2, "Utility commands: status, help, refresh, clear, pause,");
  printAt(48, 385, 2, "resume, reboot, sleep");
  printAt(48, 460, 2, "Deep sleep wakes by WAKE button, reset, or power cycle.");
  inkplate.display();
}

}  // namespace

void setup() {
  Serial.begin(USB_BAUD);
  deviceSerial.begin(DEVICE_BAUD, SERIAL_8N1);

  if (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != RESET) {
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
    HAL_PWR_DisableWakeUpPin(PWR_WAKEUP_PIN4);
  }
  HAL_PWR_EnableWakeUpPin(PWR_WAKEUP_PIN4_LOW);

  if (!inkplate.begin(INKPLATE_BLACKWHITE)) {
    Serial.println("Inkplate display initialization failed");
    while (true) delay(1000);
  }
  inkplate.setRotation(0);
  inkplate.setTextWrap(false);
  inkplate.setFullUpdateTreshold(FULL_REFRESH_THRESHOLD);
  inkplate.clearDisplay();
  inkplate.display();
  drawWaitingScreen();
  Serial.println("Inkplate lake display ready");
}

void loop() {
  consumeSerial(deviceSerial, deviceReader);
  consumeSerial(Serial, usbReader);
  delay(2);
}
