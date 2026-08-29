#pragma once

#include <stddef.h>
#include <stdint.h>
#include <math.h>

namespace logger_core {

struct WeatherSample {
  float airTemperatureC = NAN;
  float relativeHumidityPct = NAN;
  float barometricPressureHpa = NAN;
  float windSpeedMs = NAN;
  float windDirectionDeg = NAN;
  float rainfallAccumulatedMm = NAN;
  float lightLux = NAN;
};

struct WeatherSummary {
  uint16_t sampleCount = 0;
  float airTemperatureSum = 0;
  float airTemperatureMin = NAN;
  float airTemperatureMax = NAN;
  float relativeHumiditySum = 0;
  float relativeHumidityMin = NAN;
  float relativeHumidityMax = NAN;
  float barometricPressureSum = 0;
  float barometricPressureMin = NAN;
  float barometricPressureMax = NAN;
  float windSpeedSum = 0;
  float windSpeedMin = NAN;
  float windSpeedMax = NAN;
  float lightSum = 0;
  float lightMin = NAN;
  float lightMax = NAN;
  float windDirectionSinSum = 0;
  float windDirectionCosSum = 0;
  float rainfallFirstMm = NAN;
  float rainfallLastMm = NAN;
  bool rainfallCounterReset = false;
};

enum class LogScheduleDecision : uint8_t {
  NOT_DUE,
  BOUNDARY,
  CATCH_UP
};

struct LogScheduleState {
  bool initialized = false;
  int64_t latestIntervalKey = INT64_MIN;
  uint32_t catchUpCount = 0;
};

struct RetryScheduleState {
  bool attempted = false;
  bool lastAttemptSucceeded = false;
  uint32_t lastAttemptMs = 0;
  uint32_t nextAttemptMs = 0;
  uint32_t consecutiveFailures = 0;
  uint32_t attemptCount = 0;
};

struct PeriodicPollState {
  bool polled = false;
  uint32_t lastPollMs = 0;
};

struct PowerMonitorSnapshot {
  bool present = false;
  bool valid = false;
  float busVoltageV = NAN;
  float currentA = NAN;
  float powerW = NAN;
};

struct PowerSnapshot {
  PowerMonitorSnapshot batteryOutput;
  PowerMonitorSnapshot solarInput;
};

using NtpSyncState = RetryScheduleState;
using SensorDiscoveryState = RetryScheduleState;

enum class UploadEndpointMode : uint8_t {
  GOOGLE_APPS_SCRIPT,
  FESLABS_INGEST
};

enum class UploadOutcome : uint8_t {
  ACCEPTED,
  TRANSIENT_FAILURE,
  DEFERRED,
  PERMANENT_REJECTION
};

enum class FreshUploadAction : uint8_t {
  COMPLETE,
  ENQUEUE,
  DROP
};

enum class BacklogUploadAction : uint8_t {
  DEQUEUE,
  RETAIN,
  DEQUEUE_REJECTED
};

struct UploadCooldownState {
  uint32_t consecutiveFailures = 0;
  uint32_t nextAllowedMs = 0;
};

float batteryChargePercent(bool voltageValid, float voltageV);
bool solarCharging(bool currentValid, float currentA);

uint16_t modbusCrc16(const uint8_t* data, size_t length);
bool isValidModbusResponse(const uint8_t* response, size_t length,
                           uint8_t slaveId, uint8_t functionCode,
                           uint8_t expectedByteCount);
int findValidModbusResponse(const uint8_t* buffer, size_t length,
                            uint8_t slaveId, uint8_t functionCode,
                            uint8_t expectedByteCount);

uint32_t exponentialBackoff(uint32_t initialMs, uint32_t maximumMs,
                            uint32_t failureCount);
bool deadlinePending(uint32_t nowMs, uint32_t deadlineMs);
bool retryDue(uint32_t nowMs, const RetryScheduleState& state);
void recordRetryResult(RetryScheduleState& state, uint32_t nowMs,
                       bool succeeded, uint32_t successIntervalMs,
                       uint32_t retryInitialMs, uint32_t retryMaximumMs);
uint32_t retryCooldownRemaining(uint32_t nowMs,
                                const RetryScheduleState& state);
bool periodicPollDue(uint32_t nowMs, const PeriodicPollState& state,
                     uint32_t intervalMs);
void recordPeriodicPoll(PeriodicPollState& state, uint32_t nowMs);
void copyPowerSnapshot(const PowerSnapshot& source,
                       PowerMonitorSnapshot& batteryOutput,
                       PowerMonitorSnapshot& solarInput);
bool ntpSyncDue(uint32_t nowMs, const NtpSyncState& state);
void recordNtpSyncResult(NtpSyncState& state, uint32_t nowMs, bool succeeded,
                         bool clockValid, uint32_t successIntervalMs,
                         uint32_t invalidClockRetryInitialMs,
                         uint32_t invalidClockRetryMaximumMs,
                         uint32_t validClockRetryInitialMs,
                         uint32_t validClockRetryMaximumMs);
uint32_t ntpCooldownRemaining(uint32_t nowMs, const NtpSyncState& state);

bool sensorDiscoveryDue(uint32_t nowMs, bool sensorKnown,
                        const SensorDiscoveryState& state);
void recordSensorDiscoveryResult(SensorDiscoveryState& state, uint32_t nowMs,
                                 bool succeeded, uint32_t retryInitialMs,
                                 uint32_t retryMaximumMs);
uint32_t sensorDiscoveryCooldownRemaining(
    uint32_t nowMs, bool sensorKnown, const SensorDiscoveryState& state);

bool shouldStartHttpServer(bool wifiConnected, bool serverStarted);

UploadOutcome classifyUploadStatus(int statusCode,
                                   UploadEndpointMode endpointMode);
bool shouldRetryUpload(UploadOutcome outcome, uint32_t completedAttempts,
                       uint32_t maximumAttempts);
FreshUploadAction freshUploadAction(UploadOutcome outcome);
BacklogUploadAction backlogUploadAction(UploadOutcome outcome);
void recordUploadOperationOutcome(
    UploadCooldownState& state, uint32_t nowMs, UploadOutcome outcome,
    uint32_t retryInitialMs, uint32_t retryMaximumMs);

void addWeatherSample(WeatherSummary& summary, const WeatherSample& sample);
float averageWindDirectionDegrees(const WeatherSummary& summary);
float rainfallIntervalMm(const WeatherSummary& summary);

bool sensorIdentityValid(uint32_t serialNumber, uint8_t firmwareMajor,
                         uint8_t firmwareMinor, uint16_t firmwareBeta);

LogScheduleDecision observeUtcLogSchedule(
    bool clockValid, int64_t utcEpochSeconds, int intervalMinutes,
    int boundaryWindowSeconds, LogScheduleState& state);

}  // namespace logger_core
