#include "logger_core/domain_logic.h"

#include <math.h>

namespace logger_core {

float batteryChargePercent(bool voltageValid, float voltageV) {
  if (!voltageValid || !isfinite(voltageV)) return NAN;

  constexpr float emptyV = 12.0f;
  constexpr float fullV = 13.4f;
  float percent = ((voltageV - emptyV) / (fullV - emptyV)) * 100.0f;
  if (percent < 0.0f) return 0.0f;
  if (percent > 100.0f) return 100.0f;
  return percent;
}

bool solarCharging(bool currentValid, float currentA) {
  return currentValid && isfinite(currentA) && currentA > 0.05f;
}

uint16_t modbusCrc16(const uint8_t* data, size_t length) {
  if (data == nullptr && length != 0) return 0;

  uint16_t crc = 0xFFFF;
  for (size_t position = 0; position < length; ++position) {
    crc ^= data[position];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if ((crc & 0x0001U) != 0) {
        crc = (crc >> 1U) ^ 0xA001U;
      } else {
        crc >>= 1U;
      }
    }
  }
  return crc;
}

bool isValidModbusResponse(const uint8_t* response, size_t length,
                           uint8_t slaveId, uint8_t functionCode,
                           uint8_t expectedByteCount) {
  const size_t expectedLength = static_cast<size_t>(expectedByteCount) + 5U;
  if (response == nullptr || length != expectedLength || length < 5U) return false;
  if (response[0] != slaveId || response[1] != functionCode ||
      response[2] != expectedByteCount) {
    return false;
  }

  const uint16_t receivedCrc =
      static_cast<uint16_t>(response[length - 2]) |
      (static_cast<uint16_t>(response[length - 1]) << 8U);
  return receivedCrc == modbusCrc16(response, length - 2U);
}

int findValidModbusResponse(const uint8_t* buffer, size_t length,
                            uint8_t slaveId, uint8_t functionCode,
                            uint8_t expectedByteCount) {
  const size_t expectedLength = static_cast<size_t>(expectedByteCount) + 5U;
  if (buffer == nullptr || length < expectedLength) return -1;

  for (size_t offset = 0; offset + expectedLength <= length; ++offset) {
    if (isValidModbusResponse(buffer + offset, expectedLength, slaveId,
                              functionCode, expectedByteCount)) {
      return static_cast<int>(offset);
    }
  }
  return -1;
}

uint32_t exponentialBackoff(uint32_t initialMs, uint32_t maximumMs,
                            uint32_t failureCount) {
  if (failureCount == 0 || initialMs >= maximumMs) return failureCount == 0 ? 0 : maximumMs;

  uint32_t cooldown = initialMs;
  for (uint32_t failure = 1; failure < failureCount; ++failure) {
    if (cooldown >= maximumMs / 2U) return maximumMs;
    cooldown *= 2U;
  }
  return cooldown > maximumMs ? maximumMs : cooldown;
}

bool deadlinePending(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(deadlineMs - nowMs) > 0;
}

bool retryDue(uint32_t nowMs, const RetryScheduleState& state) {
  return !state.attempted || !deadlinePending(nowMs, state.nextAttemptMs);
}

void recordRetryResult(RetryScheduleState& state, uint32_t nowMs,
                       bool succeeded, uint32_t successIntervalMs,
                       uint32_t retryInitialMs, uint32_t retryMaximumMs) {
  state.attempted = true;
  state.lastAttemptSucceeded = succeeded;
  state.lastAttemptMs = nowMs;
  if (state.attemptCount != UINT32_MAX) ++state.attemptCount;

  if (succeeded) {
    state.consecutiveFailures = 0;
    state.nextAttemptMs = nowMs + successIntervalMs;
    return;
  }

  if (state.consecutiveFailures != UINT32_MAX) {
    ++state.consecutiveFailures;
  }
  state.nextAttemptMs =
      nowMs + exponentialBackoff(retryInitialMs, retryMaximumMs,
                                 state.consecutiveFailures);
}

uint32_t retryCooldownRemaining(uint32_t nowMs,
                                const RetryScheduleState& state) {
  return state.attempted && deadlinePending(nowMs, state.nextAttemptMs)
             ? state.nextAttemptMs - nowMs
             : 0;
}

bool periodicPollDue(uint32_t nowMs, const PeriodicPollState& state,
                     uint32_t intervalMs) {
  return !state.polled || (nowMs - state.lastPollMs) >= intervalMs;
}

void recordPeriodicPoll(PeriodicPollState& state, uint32_t nowMs) {
  state.polled = true;
  state.lastPollMs = nowMs;
}

void copyPowerSnapshot(const PowerSnapshot& source,
                       PowerMonitorSnapshot& batteryOutput,
                       PowerMonitorSnapshot& solarInput) {
  batteryOutput = source.batteryOutput;
  solarInput = source.solarInput;
}

bool ntpSyncDue(uint32_t nowMs, const NtpSyncState& state) {
  return retryDue(nowMs, state);
}

void recordNtpSyncResult(NtpSyncState& state, uint32_t nowMs, bool succeeded,
                         bool clockValid, uint32_t successIntervalMs,
                         uint32_t invalidClockRetryInitialMs,
                         uint32_t invalidClockRetryMaximumMs,
                         uint32_t validClockRetryInitialMs,
                         uint32_t validClockRetryMaximumMs) {
  recordRetryResult(
      state, nowMs, succeeded, successIntervalMs,
      clockValid ? validClockRetryInitialMs : invalidClockRetryInitialMs,
      clockValid ? validClockRetryMaximumMs : invalidClockRetryMaximumMs);
}

uint32_t ntpCooldownRemaining(uint32_t nowMs, const NtpSyncState& state) {
  return retryCooldownRemaining(nowMs, state);
}

bool sensorDiscoveryDue(uint32_t nowMs, bool sensorKnown,
                        const SensorDiscoveryState& state) {
  return !sensorKnown && retryDue(nowMs, state);
}

void recordSensorDiscoveryResult(SensorDiscoveryState& state, uint32_t nowMs,
                                 bool succeeded, uint32_t retryInitialMs,
                                 uint32_t retryMaximumMs) {
  recordRetryResult(state, nowMs, succeeded, 0, retryInitialMs,
                    retryMaximumMs);
}

uint32_t sensorDiscoveryCooldownRemaining(
    uint32_t nowMs, bool sensorKnown, const SensorDiscoveryState& state) {
  return sensorKnown ? 0 : retryCooldownRemaining(nowMs, state);
}

bool shouldStartHttpServer(bool wifiConnected, bool serverStarted) {
  return wifiConnected && !serverStarted;
}

UploadOutcome classifyUploadStatus(int statusCode,
                                   UploadEndpointMode endpointMode) {
  if (statusCode >= 200 && statusCode < 300) {
    return UploadOutcome::ACCEPTED;
  }

  if (endpointMode == UploadEndpointMode::GOOGLE_APPS_SCRIPT &&
      (statusCode == 301 || statusCode == 302 || statusCode == 303 ||
       statusCode == 307 || statusCode == 308)) {
    return UploadOutcome::ACCEPTED;
  }

  if (statusCode <= 0 || statusCode == 408 || statusCode == 425 ||
      statusCode == 429 || statusCode >= 500) {
    return UploadOutcome::TRANSIENT_FAILURE;
  }

  switch (statusCode) {
    case 400:
    case 401:
    case 403:
    case 404:
    case 409:
    case 413:
    case 415:
    case 422:
      return UploadOutcome::PERMANENT_REJECTION;
    default:
      break;
  }

  // Unknown redirects are not accepted, but may be repaired by endpoint
  // configuration. Other client errors are request-specific poison entries.
  if (statusCode >= 300 && statusCode < 400) {
    return UploadOutcome::TRANSIENT_FAILURE;
  }
  if (statusCode >= 400 && statusCode < 500) {
    return UploadOutcome::PERMANENT_REJECTION;
  }
  return UploadOutcome::TRANSIENT_FAILURE;
}

bool shouldRetryUpload(UploadOutcome outcome, uint32_t completedAttempts,
                       uint32_t maximumAttempts) {
  return outcome == UploadOutcome::TRANSIENT_FAILURE &&
         completedAttempts < maximumAttempts;
}

FreshUploadAction freshUploadAction(UploadOutcome outcome) {
  switch (outcome) {
    case UploadOutcome::ACCEPTED:
      return FreshUploadAction::COMPLETE;
    case UploadOutcome::PERMANENT_REJECTION:
      return FreshUploadAction::DROP;
    case UploadOutcome::TRANSIENT_FAILURE:
    case UploadOutcome::DEFERRED:
      return FreshUploadAction::ENQUEUE;
  }
  return FreshUploadAction::ENQUEUE;
}

BacklogUploadAction backlogUploadAction(UploadOutcome outcome) {
  switch (outcome) {
    case UploadOutcome::ACCEPTED:
      return BacklogUploadAction::DEQUEUE;
    case UploadOutcome::PERMANENT_REJECTION:
      return BacklogUploadAction::DEQUEUE_REJECTED;
    case UploadOutcome::TRANSIENT_FAILURE:
    case UploadOutcome::DEFERRED:
      return BacklogUploadAction::RETAIN;
  }
  return BacklogUploadAction::RETAIN;
}

void recordUploadOperationOutcome(
    UploadCooldownState& state, uint32_t nowMs, UploadOutcome outcome,
    uint32_t retryInitialMs, uint32_t retryMaximumMs) {
  if (outcome == UploadOutcome::DEFERRED) return;

  if (outcome != UploadOutcome::TRANSIENT_FAILURE) {
    state.consecutiveFailures = 0;
    state.nextAllowedMs = 0;
    return;
  }

  if (state.consecutiveFailures != UINT32_MAX) {
    ++state.consecutiveFailures;
  }
  state.nextAllowedMs =
      nowMs + exponentialBackoff(retryInitialMs, retryMaximumMs,
                                 state.consecutiveFailures);
}

namespace {

constexpr float PI_F = 3.14159265358979323846f;

void addWeatherValue(float value, float& sum, float& minimum, float& maximum) {
  sum += value;
  if (!isfinite(minimum) || value < minimum) minimum = value;
  if (!isfinite(maximum) || value > maximum) maximum = value;
}

}  // namespace

void addWeatherSample(WeatherSummary& summary, const WeatherSample& sample) {
  if (summary.sampleCount == 0) {
    summary.rainfallFirstMm = sample.rainfallAccumulatedMm;
  } else if (sample.rainfallAccumulatedMm < summary.rainfallLastMm) {
    summary.rainfallCounterReset = true;
  }

  ++summary.sampleCount;
  summary.rainfallLastMm = sample.rainfallAccumulatedMm;
  addWeatherValue(sample.airTemperatureC, summary.airTemperatureSum,
                  summary.airTemperatureMin, summary.airTemperatureMax);
  addWeatherValue(sample.relativeHumidityPct, summary.relativeHumiditySum,
                  summary.relativeHumidityMin, summary.relativeHumidityMax);
  addWeatherValue(sample.barometricPressureHpa, summary.barometricPressureSum,
                  summary.barometricPressureMin, summary.barometricPressureMax);
  addWeatherValue(sample.windSpeedMs, summary.windSpeedSum,
                  summary.windSpeedMin, summary.windSpeedMax);
  addWeatherValue(sample.lightLux, summary.lightSum, summary.lightMin,
                  summary.lightMax);

  const float radians = sample.windDirectionDeg * PI_F / 180.0f;
  summary.windDirectionSinSum += sinf(radians);
  summary.windDirectionCosSum += cosf(radians);
}

float averageWindDirectionDegrees(const WeatherSummary& summary) {
  if (summary.sampleCount == 0) return NAN;

  float direction =
      atan2f(summary.windDirectionSinSum, summary.windDirectionCosSum) *
      180.0f / PI_F;
  if (direction < 0) direction += 360.0f;
  return direction;
}

float rainfallIntervalMm(const WeatherSummary& summary) {
  if (summary.sampleCount == 0 || summary.rainfallCounterReset ||
      !isfinite(summary.rainfallFirstMm)) {
    return NAN;
  }
  return summary.rainfallLastMm - summary.rainfallFirstMm;
}

bool sensorIdentityValid(uint32_t serialNumber, uint8_t firmwareMajor,
                         uint8_t firmwareMinor, uint16_t firmwareBeta) {
  (void)firmwareBeta;
  if (serialNumber == 0 || serialNumber == UINT32_MAX) return false;
  return firmwareMajor != 0 || firmwareMinor != 0;
}

LogScheduleDecision observeUtcLogSchedule(
    bool clockValid, int64_t utcEpochSeconds, int intervalMinutes,
    int boundaryWindowSeconds, LogScheduleState& state) {
  if (!clockValid || utcEpochSeconds < 0 || intervalMinutes <= 0 ||
      boundaryWindowSeconds <= 0) {
    return LogScheduleDecision::NOT_DUE;
  }

  const int64_t intervalSeconds =
      static_cast<int64_t>(intervalMinutes) * 60;
  if (boundaryWindowSeconds > intervalSeconds) {
    return LogScheduleDecision::NOT_DUE;
  }

  const int64_t intervalKey = utcEpochSeconds / intervalSeconds;
  const int64_t secondsIntoInterval = utcEpochSeconds % intervalSeconds;
  const bool withinBoundaryWindow =
      secondsIntoInterval < boundaryWindowSeconds;

  if (!state.initialized) {
    state.initialized = true;
    state.latestIntervalKey = intervalKey;
    return withinBoundaryWindow ? LogScheduleDecision::BOUNDARY
                                : LogScheduleDecision::NOT_DUE;
  }

  if (intervalKey <= state.latestIntervalKey) {
    return LogScheduleDecision::NOT_DUE;
  }

  state.latestIntervalKey = intervalKey;
  if (withinBoundaryWindow) {
    return LogScheduleDecision::BOUNDARY;
  }

  if (state.catchUpCount != UINT32_MAX) {
    ++state.catchUpCount;
  }
  return LogScheduleDecision::CATCH_UP;
}

}  // namespace logger_core
