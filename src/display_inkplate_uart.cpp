#include "config.h"

#if defined(LOGGER_BOARD_GIGA)
#include "giga_board_config.h"
#include "inkplate_6motion_display/inkplate_protocol.h"

namespace {

bool refreshPending = true;
uint32_t nextSequence = 1;
uint32_t lastReadingRevision = UINT32_MAX;
uint32_t lastWeatherRevision = UINT32_MAX;
logger_core::SiteHealth lastHealth = logger_core::SiteHealth::CRITICAL;
uint32_t lastRefreshMs = 0;
uint32_t lastWakeRequestMs = 0;
uint32_t refreshCountLocal = 0;
uint32_t linkFailureCountLocal = 0;
String lastRefreshUtcValue;
String lastWakeRequestUtcValue;
String lastErrorValue = "not detected";
bool serialStarted = false;
bool renderingPaused = false;

const char* healthProtocolName(logger_core::SiteHealth health) {
  switch (health) {
    case logger_core::SiteHealth::HEALTHY: return "HEALTHY";
    case logger_core::SiteHealth::DEGRADED: return "DEGRADED";
    case logger_core::SiteHealth::CRITICAL: return "CRITICAL";
  }
  return "CRITICAL";
}

void appendField(String& payload, const char* key, const String& value) {
  if (payload.length() > 0) payload += ';';
  payload += key;
  payload += '=';
  payload += value;
}

void appendBoolField(String& payload, const char* key, bool value) {
  appendField(payload, key, value ? "1" : "0");
}

void appendFiniteFloat(
    String& payload, const char* key, float value, uint8_t decimals) {
  if (isfinite(value)) appendField(payload, key, String(value, decimals));
}

String snapshotPayload(const logger_core::SiteSnapshot& snapshot) {
  String payload;
  payload.reserve(900);
  appendField(payload, "device", DEVICE_ID);
  const String timestamp = nowUtcString();
  appendField(
      payload, "timestamp",
      timestamp.length() > 0 ? timestamp : String("unavailable"));
  appendField(payload, "health", healthProtocolName(snapshot.health));
  appendBoolField(payload, "clock", snapshot.clockValid);
  appendBoolField(payload, "wifi", snapshot.wifiConnected);
  appendField(payload, "rssi", String(snapshot.wifiRssiDbm));
  if (snapshot.wifiConnected) {
    appendField(
        payload, "ip",
        String(snapshot.ipAddress[0]) + "." + String(snapshot.ipAddress[1]) +
            "." + String(snapshot.ipAddress[2]) + "." +
            String(snapshot.ipAddress[3]));
  } else {
    appendField(payload, "ip", "offline");
  }
  appendBoolField(payload, "sensor_found", snapshot.sensorFound);
  appendField(payload, "modbus_id", String(snapshot.sensorModbusId));
  appendField(payload, "serial", String(snapshot.sensorSerialNumber));
  appendBoolField(payload, "water_valid", snapshot.waterValid);
  if (snapshot.waterValid) {
    appendFiniteFloat(payload, "water_level_m", snapshot.waterLevelM, 4);
    appendFiniteFloat(
        payload, "water_temp_c", snapshot.waterTemperatureC, 3);
  }
  appendField(
      payload, "probe_age_s",
      snapshot.probeAgeValid ? String(snapshot.probeAgeSeconds) : String("-1"));
  appendBoolField(payload, "weather_enabled", snapshot.weatherEnabled);
  appendBoolField(payload, "weather_present", snapshot.weatherPresent);
  appendBoolField(payload, "weather_valid", snapshot.weatherValid);
  if (snapshot.weatherValid) {
    appendFiniteFloat(
        payload, "air_temp_c", snapshot.airTemperatureC, 2);
    appendFiniteFloat(
        payload, "humidity_pct", snapshot.relativeHumidityPct, 1);
    appendFiniteFloat(
        payload, "pressure_hpa", snapshot.barometricPressureHpa, 1);
    appendFiniteFloat(payload, "wind_m_s", snapshot.windSpeedMs, 2);
    appendFiniteFloat(payload, "wind_deg", snapshot.windDirectionDeg, 1);
  }
  const bool rainfallValid = isfinite(snapshot.rainfallIntervalMm);
  appendBoolField(payload, "rain_valid", rainfallValid);
  if (rainfallValid) {
    appendFiniteFloat(
        payload, "rain_mm", snapshot.rainfallIntervalMm, 2);
  }
  appendBoolField(payload, "battery_valid", snapshot.batteryValid);
  if (snapshot.batteryValid) {
    appendFiniteFloat(
        payload, "battery_v", snapshot.batteryVoltageV, 3);
    appendFiniteFloat(
        payload, "battery_pct", snapshot.batteryChargePct, 1);
  }
  appendBoolField(
      payload, "battery_extrema_valid", snapshot.batteryExtrema24hValid);
  if (snapshot.batteryExtrema24hValid) {
    appendFiniteFloat(
        payload, "battery_min_v", snapshot.batteryVoltageMin24hV, 3);
    appendFiniteFloat(
        payload, "battery_max_v", snapshot.batteryVoltageMax24hV, 3);
  }
  appendBoolField(payload, "solar_valid", snapshot.solarValid);
  if (snapshot.solarValid) {
    appendFiniteFloat(payload, "solar_v", snapshot.solarVoltageV, 3);
    appendFiniteFloat(payload, "solar_a", snapshot.solarCurrentA, 3);
    appendFiniteFloat(payload, "solar_w", snapshot.solarPowerW, 2);
  }
  appendBoolField(payload, "solar_charging", snapshot.solarCharging);
  appendField(
      payload, "backlog",
      String(static_cast<unsigned long>(snapshot.backlogCount)));
  appendField(
      payload, "upload_failures",
      String(snapshot.consecutiveUploadFailures));
  appendField(
      payload, "upload_age_s",
      snapshot.uploadAgeValid ? String(snapshot.uploadAgeSeconds)
                              : String("-1"));
  return payload;
}

void drainReceive() {
  while (Serial1.available() > 0) Serial1.read();
}

bool readResponse(
    uint32_t expectedSequence, uint32_t timeoutMs,
    inkplate_protocol::Frame& response) {
  static char line[inkplate_protocol::MAX_FRAME_LENGTH + 1];
  size_t length = 0;
  bool overflow = false;
  const uint32_t startedMs = millis();
  while (millis() - startedMs < timeoutMs) {
    kickSystemWatchdog();
    while (Serial1.available() > 0) {
      const int incoming = Serial1.read();
      if (incoming < 0) break;
      const char value = static_cast<char>(incoming);
      if (value == '\r') continue;
      if (value == '\n') {
        if (!overflow && length > 0) {
          line[length] = '\0';
          inkplate_protocol::Frame candidate;
          if (inkplate_protocol::parseFrame(line, candidate) ==
                  inkplate_protocol::ParseResult::OK &&
              candidate.sequence == expectedSequence &&
              (candidate.type == inkplate_protocol::FrameType::ACK ||
               candidate.type ==
                   inkplate_protocol::FrameType::ERROR_RESPONSE)) {
            response = candidate;
            return true;
          }
        }
        length = 0;
        overflow = false;
        continue;
      }
      if (overflow) continue;
      if (length == inkplate_protocol::MAX_FRAME_LENGTH) {
        overflow = true;
      } else {
        line[length++] = value;
      }
    }
    delay(1);
  }
  return false;
}

bool staleSequenceValue(const char* payload, uint32_t& value) {
  constexpr char marker[] = "reason=stale_sequence;last_sequence=";
  if (payload == nullptr || strncmp(payload, marker, sizeof(marker) - 1) != 0) {
    return false;
  }
  return inkplate_protocol::parseUint32(payload + sizeof(marker) - 1, value);
}

bool sendRequestOnce(
    inkplate_protocol::FrameType type, const String& payload,
    uint32_t sequence, uint32_t timeoutMs,
    inkplate_protocol::Frame& response) {
  const char* typeName =
      type == inkplate_protocol::FrameType::SNAPSHOT ? "SNAPSHOT" : "COMMAND";
  String body;
  body.reserve(payload.length() + 48);
  body += String(inkplate_protocol::VERSION);
  body += '|';
  body += sequence;
  body += '|';
  body += typeName;
  body += '|';
  body += payload;
  if (body.length() + 6 > inkplate_protocol::MAX_FRAME_LENGTH) {
    lastErrorValue = "request exceeds protocol limit";
    return false;
  }

  drainReceive();
  char checksum[5];
  snprintf(
      checksum, sizeof(checksum), "%04X",
      inkplate_protocol::crc16Ccitt(body.c_str(), body.length()));
  Serial1.print('@');
  Serial1.print(body);
  Serial1.print('*');
  Serial1.println(checksum);
  Serial1.flush();
  if (!readResponse(sequence, timeoutMs, response)) {
    lastErrorValue = "response timeout";
    return false;
  }
  return true;
}

bool sendRequest(
    inkplate_protocol::FrameType type, const String& payload,
    uint32_t timeoutMs, String& responsePayload) {
  if (nextSequence == 0) {
    lastErrorValue = "sequence exhausted; reboot Inkplate";
    return false;
  }
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    const uint32_t sequence = nextSequence++;
    inkplate_protocol::Frame response;
    if (!sendRequestOnce(type, payload, sequence, timeoutMs, response)) {
      return false;
    }
    responsePayload = response.payload;
    if (response.type == inkplate_protocol::FrameType::ACK) {
      lastErrorValue = "";
      return true;
    }
    uint32_t remoteSequence = 0;
    if (attempt == 0 && staleSequenceValue(response.payload, remoteSequence) &&
        remoteSequence < UINT32_MAX) {
      nextSequence = remoteSequence + 1;
      continue;
    }
    lastErrorValue = String("Inkplate error: ") + response.payload;
    return false;
  }
  return false;
}

bool detectInkplate() {
  String response;
  if (!sendRequest(
          inkplate_protocol::FrameType::COMMAND, "status",
          GIGA_INKPLATE_DETECT_TIMEOUT_MS, response)) {
    return false;
  }
  if (response.indexOf("state=") < 0) return false;
  renderingPaused = response.indexOf("state=paused") >= 0;
  return true;
}

bool sendSnapshot(const logger_core::SiteSnapshot& snapshot) {
  String response;
  if (!sendRequest(
          inkplate_protocol::FrameType::SNAPSHOT,
          snapshotPayload(snapshot), GIGA_INKPLATE_ACK_TIMEOUT_MS,
          response)) {
    ++linkFailureCountLocal;
    displayPresent = false;
    displayAwake = false;
    return false;
  }
  lastReadingRevision = snapshot.readingRevision;
  lastWeatherRevision = snapshot.weatherRevision;
  lastHealth = snapshot.health;
  lastRefreshMs = millis();
  lastRefreshUtcValue = nowUtcString();
  ++refreshCountLocal;
  displayPresent = true;
  displayAwake = !renderingPaused;
  refreshPending = false;
  return true;
}

}  // namespace

bool initInkplateUartDisplay() {
  if (!serialStarted) {
    Serial1.begin(GIGA_INKPLATE_BAUD);
    serialStarted = true;
  }
  if (!detectInkplate()) {
    Serial.print("Inkplate UART display not detected: ");
    Serial.println(lastErrorValue);
    return false;
  }
  displayPresent = true;
  displayAwake = !renderingPaused;
  refreshPending = true;
  Serial.println("Inkplate 6MOTION detected on Serial1 (D1 TX, D0 RX)");
  return true;
}

void updateInkplateUartDisplay() {
  if (!displayPresent) return;
  const logger_core::SiteSnapshot snapshot = currentSiteSnapshot();
  const bool changed =
      snapshot.readingRevision != lastReadingRevision ||
      snapshot.weatherRevision != lastWeatherRevision ||
      snapshot.health != lastHealth;
  const bool intervalElapsed =
      lastRefreshMs == 0 ||
      millis() - lastRefreshMs >= GIGA_INKPLATE_REFRESH_INTERVAL_MS;
  if (!refreshPending && !changed && !intervalElapsed) return;
  sendSnapshot(snapshot);
}

void wakeInkplateUartDisplay() {
  lastWakeRequestMs = millis();
  lastWakeRequestUtcValue = nowUtcString();
  refreshPending = true;
}

void sleepInkplateUartDisplay() {
  String response;
  if (sendInkplateCommand("pause", response)) displayAwake = false;
}

String lastInkplateWakeRequestUtc() {
  return lastWakeRequestUtcValue;
}

String lastInkplateRefreshUtc() {
  return lastRefreshUtcValue;
}

String lastInkplateWakeRequestAge() {
  return lastWakeRequestMs == 0
             ? String("never")
             : millisAgeString(lastWakeRequestMs);
}

String lastInkplateRefreshAge() {
  return lastRefreshMs == 0
             ? String("never")
             : millisAgeString(lastRefreshMs);
}

uint32_t inkplateRefreshCount() {
  return refreshCountLocal;
}

uint32_t inkplateLinkFailureCount() {
  return linkFailureCountLocal;
}

String inkplateLastError() {
  return lastErrorValue;
}

bool sendInkplateCommand(const char* command, String& response) {
  if (command == nullptr || command[0] == '\0') {
    lastErrorValue = "empty command";
    return false;
  }
  const bool success = sendRequest(
      inkplate_protocol::FrameType::COMMAND, command,
      GIGA_INKPLATE_ACK_TIMEOUT_MS, response);
  if (!success) {
    ++linkFailureCountLocal;
    displayPresent = false;
    displayAwake = false;
    return false;
  }
  displayPresent = true;
  if (strcmp(command, "pause") == 0 ||
      strcmp(command, "clear") == 0) {
    renderingPaused = true;
    displayAwake = false;
  }
  if (strcmp(command, "resume") == 0) {
    renderingPaused = false;
    displayAwake = true;
    refreshPending = true;
  }
  if (strcmp(command, "sleep") == 0 ||
      strcmp(command, "reboot") == 0) {
    renderingPaused = false;
    displayPresent = false;
    displayAwake = false;
  }
  if (strcmp(command, "refresh") == 0) {
    lastRefreshMs = millis();
    lastRefreshUtcValue = nowUtcString();
    ++refreshCountLocal;
  }
  return true;
}
#endif
