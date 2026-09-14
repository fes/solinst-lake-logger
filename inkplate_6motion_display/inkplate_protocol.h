#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace inkplate_protocol {

constexpr uint8_t VERSION = 1;
constexpr size_t MAX_FRAME_LENGTH = 1536;
constexpr size_t MAX_PAYLOAD_LENGTH = 1400;

enum class FrameType : uint8_t {
  SNAPSHOT,
  COMMAND,
  ACK,
  ERROR_RESPONSE
};

enum class ParseResult : uint8_t {
  OK,
  EMPTY,
  TOO_LONG,
  BAD_PREFIX,
  BAD_CHECKSUM,
  BAD_FORMAT,
  BAD_VERSION,
  BAD_SEQUENCE,
  BAD_TYPE
};

struct Frame {
  uint32_t sequence = 0;
  FrameType type = FrameType::COMMAND;
  char payload[MAX_PAYLOAD_LENGTH + 1] = {};
};

struct DisplaySnapshot {
  char device[33] = {};
  char timestampUtc[25] = {};
  char health[10] = "CRITICAL";
  bool clockValid = false;
  bool wifiConnected = false;
  int32_t wifiRssiDbm = 0;
  char ipAddress[16] = {};
  bool sensorFound = false;
  uint32_t sensorModbusId = 0;
  uint32_t sensorSerialNumber = 0;
  bool waterValid = false;
  float waterLevelM = 0;
  float waterTemperatureC = 0;
  int32_t probeAgeSeconds = -1;
  bool weatherEnabled = false;
  bool weatherPresent = false;
  bool weatherValid = false;
  float airTemperatureC = 0;
  float relativeHumidityPct = 0;
  float barometricPressureHpa = 0;
  float windSpeedMs = 0;
  float windDirectionDeg = 0;
  float rainfallIntervalMm = 0;
  bool rainfallValid = false;
  bool batteryValid = false;
  float batteryVoltageV = 0;
  float batteryChargePct = 0;
  bool batteryExtremaValid = false;
  float batteryVoltageMin24hV = 0;
  float batteryVoltageMax24hV = 0;
  bool solarValid = false;
  float solarVoltageV = 0;
  float solarCurrentA = 0;
  float solarPowerW = 0;
  bool solarCharging = false;
  uint32_t backlogCount = 0;
  uint32_t consecutiveUploadFailures = 0;
  int32_t uploadAgeSeconds = -1;
};

inline uint16_t crc16Ccitt(const char* data, size_t length) {
  uint16_t crc = 0xFFFFU;
  if (data == nullptr) return crc;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(
        static_cast<uint8_t>(data[i])) << 8U;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000U) != 0
                ? static_cast<uint16_t>((crc << 1U) ^ 0x1021U)
                : static_cast<uint16_t>(crc << 1U);
    }
  }
  return crc;
}

inline bool parseUint32(const char* value, uint32_t& output) {
  if (value == nullptr || value[0] == '\0') return false;
  uint32_t parsed = 0;
  for (const char* cursor = value; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
    if (parsed > (UINT32_MAX - digit) / 10U) return false;
    parsed = parsed * 10U + digit;
  }
  output = parsed;
  return true;
}

inline bool parseInt32(const char* value, int32_t& output) {
  if (value == nullptr || value[0] == '\0') return false;
  const bool negative = value[0] == '-';
  const char* cursor = negative ? value + 1 : value;
  if (*cursor == '\0') return false;
  const uint32_t limit =
      negative ? UINT32_C(2147483648) : UINT32_C(2147483647);
  uint32_t magnitude = 0;
  for (; *cursor != '\0'; ++cursor) {
    if (*cursor < '0' || *cursor > '9') return false;
    const uint32_t digit = static_cast<uint32_t>(*cursor - '0');
    if (magnitude > (limit - digit) / 10U) return false;
    magnitude = magnitude * 10U + digit;
  }
  if (negative) {
    output = magnitude == UINT32_C(2147483648)
                 ? INT32_MIN
                 : -static_cast<int32_t>(magnitude);
  } else {
    output = static_cast<int32_t>(magnitude);
  }
  return true;
}

inline bool parseBool(const char* value, bool& output) {
  if (strcmp(value, "1") == 0) {
    output = true;
    return true;
  }
  if (strcmp(value, "0") == 0) {
    output = false;
    return true;
  }
  return false;
}

inline bool parseFloat(const char* value, float& output) {
  if (value == nullptr || value[0] == '\0') return false;
  char* end = nullptr;
  const float parsed = strtof(value, &end);
  if (end == value || *end != '\0' || !isfinite(parsed)) return false;
  output = parsed;
  return true;
}

inline bool copyToken(
    const char* value, char* output, size_t outputLength,
    bool allowSpace = false) {
  if (value == nullptr || value[0] == '\0' || outputLength == 0 ||
      strlen(value) >= outputLength) {
    return false;
  }
  for (const char* cursor = value; *cursor != '\0'; ++cursor) {
    const unsigned char c = static_cast<unsigned char>(*cursor);
    const bool safe = (c >= 'a' && c <= 'z') ||
                      (c >= 'A' && c <= 'Z') ||
                      (c >= '0' && c <= '9') ||
                      strchr("._:+-/", c) != nullptr ||
                      (allowSpace && c == ' ');
    if (!safe) return false;
  }
  memcpy(output, value, strlen(value) + 1);
  return true;
}

inline int hexadecimalValue(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

inline ParseResult parseFrame(const char* line, Frame& output) {
  if (line == nullptr || line[0] == '\0') return ParseResult::EMPTY;
  const size_t length = strlen(line);
  if (length > MAX_FRAME_LENGTH) return ParseResult::TOO_LONG;
  if (line[0] != '@') return ParseResult::BAD_PREFIX;

  const char* star = strrchr(line, '*');
  if (star == nullptr || strlen(star + 1) != 4) {
    return ParseResult::BAD_FORMAT;
  }
  uint16_t expectedCrc = 0;
  for (size_t i = 0; i < 4; ++i) {
    const int nibble = hexadecimalValue(star[1 + i]);
    if (nibble < 0) return ParseResult::BAD_FORMAT;
    expectedCrc =
        static_cast<uint16_t>((expectedCrc << 4U) | static_cast<uint16_t>(nibble));
  }
  const size_t bodyLength = static_cast<size_t>(star - line - 1);
  if (crc16Ccitt(line + 1, bodyLength) != expectedCrc) {
    return ParseResult::BAD_CHECKSUM;
  }

  char body[MAX_FRAME_LENGTH + 1];
  memcpy(body, line + 1, bodyLength);
  body[bodyLength] = '\0';
  char* cursor = body;
  char* separators[3] = {};
  for (size_t i = 0; i < 3; ++i) {
    separators[i] = strchr(cursor, '|');
    if (separators[i] == nullptr) return ParseResult::BAD_FORMAT;
    *separators[i] = '\0';
    cursor = separators[i] + 1;
  }

  uint32_t version = 0;
  if (!parseUint32(body, version)) return ParseResult::BAD_FORMAT;
  if (version != VERSION) return ParseResult::BAD_VERSION;
  if (!parseUint32(separators[0] + 1, output.sequence)) {
    return ParseResult::BAD_SEQUENCE;
  }
  if (output.sequence == 0) return ParseResult::BAD_SEQUENCE;
  const char* type = separators[1] + 1;
  if (strcmp(type, "SNAPSHOT") == 0) {
    output.type = FrameType::SNAPSHOT;
  } else if (strcmp(type, "COMMAND") == 0) {
    output.type = FrameType::COMMAND;
  } else if (strcmp(type, "ACK") == 0) {
    output.type = FrameType::ACK;
  } else if (strcmp(type, "ERROR") == 0) {
    output.type = FrameType::ERROR_RESPONSE;
  } else {
    return ParseResult::BAD_TYPE;
  }
  const char* payload = separators[2] + 1;
  if (strlen(payload) > MAX_PAYLOAD_LENGTH) return ParseResult::TOO_LONG;
  memcpy(output.payload, payload, strlen(payload) + 1);
  return ParseResult::OK;
}

inline bool assignSnapshotField(
    DisplaySnapshot& snapshot, const char* key, const char* value,
    uint64_t& seen) {
#define FIELD(name, bit, assignment)                    \
  if (strcmp(key, name) == 0) {                         \
    if ((seen & (UINT64_C(1) << bit)) != 0) return false; \
    seen |= UINT64_C(1) << bit;                         \
    return (assignment);                                \
  }
  FIELD("device", 0, copyToken(value, snapshot.device, sizeof(snapshot.device)))
  FIELD(
      "timestamp", 1,
      copyToken(value, snapshot.timestampUtc, sizeof(snapshot.timestampUtc)))
  FIELD("health", 2, copyToken(value, snapshot.health, sizeof(snapshot.health)))
  FIELD("clock", 3, parseBool(value, snapshot.clockValid))
  FIELD("wifi", 4, parseBool(value, snapshot.wifiConnected))
  FIELD("rssi", 5, parseInt32(value, snapshot.wifiRssiDbm))
  FIELD("ip", 6, copyToken(value, snapshot.ipAddress, sizeof(snapshot.ipAddress)))
  FIELD("sensor_found", 7, parseBool(value, snapshot.sensorFound))
  FIELD("modbus_id", 8, parseUint32(value, snapshot.sensorModbusId))
  FIELD("serial", 9, parseUint32(value, snapshot.sensorSerialNumber))
  FIELD("water_valid", 10, parseBool(value, snapshot.waterValid))
  FIELD("water_level_m", 11, parseFloat(value, snapshot.waterLevelM))
  FIELD("water_temp_c", 12, parseFloat(value, snapshot.waterTemperatureC))
  FIELD("probe_age_s", 13, parseInt32(value, snapshot.probeAgeSeconds))
  FIELD("weather_enabled", 14, parseBool(value, snapshot.weatherEnabled))
  FIELD("weather_present", 15, parseBool(value, snapshot.weatherPresent))
  FIELD("weather_valid", 16, parseBool(value, snapshot.weatherValid))
  FIELD("air_temp_c", 17, parseFloat(value, snapshot.airTemperatureC))
  FIELD("humidity_pct", 18, parseFloat(value, snapshot.relativeHumidityPct))
  FIELD("pressure_hpa", 19, parseFloat(value, snapshot.barometricPressureHpa))
  FIELD("wind_m_s", 20, parseFloat(value, snapshot.windSpeedMs))
  FIELD("wind_deg", 21, parseFloat(value, snapshot.windDirectionDeg))
  FIELD("rain_valid", 22, parseBool(value, snapshot.rainfallValid))
  FIELD("rain_mm", 23, parseFloat(value, snapshot.rainfallIntervalMm))
  FIELD("battery_valid", 24, parseBool(value, snapshot.batteryValid))
  FIELD("battery_v", 25, parseFloat(value, snapshot.batteryVoltageV))
  FIELD("battery_pct", 26, parseFloat(value, snapshot.batteryChargePct))
  FIELD(
      "battery_extrema_valid", 27,
      parseBool(value, snapshot.batteryExtremaValid))
  FIELD(
      "battery_min_v", 28,
      parseFloat(value, snapshot.batteryVoltageMin24hV))
  FIELD(
      "battery_max_v", 29,
      parseFloat(value, snapshot.batteryVoltageMax24hV))
  FIELD("solar_valid", 30, parseBool(value, snapshot.solarValid))
  FIELD("solar_v", 31, parseFloat(value, snapshot.solarVoltageV))
  FIELD("solar_a", 32, parseFloat(value, snapshot.solarCurrentA))
  FIELD("solar_w", 33, parseFloat(value, snapshot.solarPowerW))
  FIELD("solar_charging", 34, parseBool(value, snapshot.solarCharging))
  FIELD("backlog", 35, parseUint32(value, snapshot.backlogCount))
  FIELD(
      "upload_failures", 36,
      parseUint32(value, snapshot.consecutiveUploadFailures))
  FIELD("upload_age_s", 37, parseInt32(value, snapshot.uploadAgeSeconds))
#undef FIELD
  return true;
}

inline bool parseSnapshot(
    const char* payload, DisplaySnapshot& output, const char*& error) {
  if (payload == nullptr || payload[0] == '\0' ||
      strlen(payload) > MAX_PAYLOAD_LENGTH) {
    error = "bad_snapshot_length";
    return false;
  }
  char copy[MAX_PAYLOAD_LENGTH + 1];
  memcpy(copy, payload, strlen(payload) + 1);
  DisplaySnapshot candidate;
  uint64_t seen = 0;
  char* field = copy;
  while (field != nullptr && field[0] != '\0') {
    char* next = strchr(field, ';');
    if (next != nullptr) {
      *next = '\0';
      ++next;
    }
    char* equals = strchr(field, '=');
    if (equals == nullptr || equals == field || equals[1] == '\0') {
      error = "bad_snapshot_field";
      return false;
    }
    *equals = '\0';
    if (!assignSnapshotField(candidate, field, equals + 1, seen)) {
      error = "bad_snapshot_value";
      return false;
    }
    field = next;
  }
  constexpr uint64_t required =
      (UINT64_C(1) << 0) | (UINT64_C(1) << 1) | (UINT64_C(1) << 2);
  if ((seen & required) != required) {
    error = "missing_snapshot_identity";
    return false;
  }
  if (strcmp(candidate.health, "HEALTHY") != 0 &&
      strcmp(candidate.health, "DEGRADED") != 0 &&
      strcmp(candidate.health, "CRITICAL") != 0) {
    error = "bad_health";
    return false;
  }
  if (candidate.probeAgeSeconds < -1 || candidate.uploadAgeSeconds < -1 ||
      candidate.sensorModbusId > UINT8_MAX) {
    error = "bad_snapshot_range";
    return false;
  }
  output = candidate;
  error = nullptr;
  return true;
}

inline const char* parseResultName(ParseResult result) {
  switch (result) {
    case ParseResult::OK: return "ok";
    case ParseResult::EMPTY: return "empty";
    case ParseResult::TOO_LONG: return "too_long";
    case ParseResult::BAD_PREFIX: return "bad_prefix";
    case ParseResult::BAD_CHECKSUM: return "bad_checksum";
    case ParseResult::BAD_FORMAT: return "bad_format";
    case ParseResult::BAD_VERSION: return "bad_version";
    case ParseResult::BAD_SEQUENCE: return "bad_sequence";
    case ParseResult::BAD_TYPE: return "bad_type";
  }
  return "unknown";
}

}  // namespace inkplate_protocol
