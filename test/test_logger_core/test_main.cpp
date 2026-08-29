#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "board_profile.h"
#include <logger_core/domain_logic.h>
#include <logger_core/http_parser.h>
#include <logger_core/modbus_codec.h>
#include <logger_core/ring_buffer.h>
#include <logger_core/rolling_extrema.h>
#include <logger_core/sc16is752_codec.h>
#include <logger_core/site_presentation.h>

namespace {

void finalizeModbusFrame(uint8_t* frame, size_t length) {
  const uint16_t crc = logger_core::modbusCrc16(frame, length - 2);
  frame[length - 2] = static_cast<uint8_t>(crc & 0xFFU);
  frame[length - 1] = static_cast<uint8_t>(crc >> 8U);
}

void testRingBufferWrapFullReuseAndBounds() {
  logger_core::RingBuffer<int, 3> queue;
  int value = -1;

  TEST_ASSERT_TRUE(queue.empty());
  TEST_ASSERT_EQUAL_UINT32(0, queue.size());
  TEST_ASSERT_EQUAL_UINT32(3, queue.capacity());
  TEST_ASSERT_FALSE(queue.front(value));
  TEST_ASSERT_FALSE(queue.back(value));
  TEST_ASSERT_FALSE(queue.pop(value));
  TEST_ASSERT_FALSE(queue.get(0, value));

  TEST_ASSERT_TRUE(queue.push(10));
  TEST_ASSERT_TRUE(queue.push(20));
  TEST_ASSERT_TRUE(queue.push(30));
  TEST_ASSERT_FALSE(queue.push(40));
  TEST_ASSERT_FALSE(queue.get(3, value));

  TEST_ASSERT_TRUE(queue.pop(value));
  TEST_ASSERT_EQUAL_INT(10, value);
  TEST_ASSERT_TRUE(queue.push(40));

  const int expected[] = {20, 30, 40};
  for (size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(queue.get(i, value));
    TEST_ASSERT_EQUAL_INT(expected[i], value);
  }
  TEST_ASSERT_TRUE(queue.front(value));
  TEST_ASSERT_EQUAL_INT(20, value);
  TEST_ASSERT_TRUE(queue.back(value));
  TEST_ASSERT_EQUAL_INT(40, value);

  for (size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(queue.pop(value));
    TEST_ASSERT_EQUAL_INT(expected[i], value);
  }
  TEST_ASSERT_TRUE(queue.empty());
  TEST_ASSERT_TRUE(queue.push(50));
  TEST_ASSERT_TRUE(queue.pop(value));
  TEST_ASSERT_EQUAL_INT(50, value);

  TEST_ASSERT_TRUE(queue.push(60));
  queue.clear();
  TEST_ASSERT_TRUE(queue.empty());
  TEST_ASSERT_TRUE(queue.push(70));
  TEST_ASSERT_TRUE(queue.front(value));
  TEST_ASSERT_EQUAL_INT(70, value);
}

void testBatteryEstimateTable() {
  struct Case {
    bool valid;
    float voltage;
    bool expectNan;
    float expected;
  };
  const Case cases[] = {
      {false, 12.7f, true, 0.0f},
      {true, NAN, true, 0.0f},
      {true, INFINITY, true, 0.0f},
      {true, -INFINITY, true, 0.0f},
      {true, 11.0f, false, 0.0f},
      {true, 12.0f, false, 0.0f},
      {true, 12.7f, false, 50.0f},
      {true, 13.4f, false, 100.0f},
      {true, 14.0f, false, 100.0f},
  };

  for (const Case& test : cases) {
    const float actual =
        logger_core::batteryChargePercent(test.valid, test.voltage);
    if (test.expectNan) {
      TEST_ASSERT_TRUE(std::isnan(actual));
    } else {
      TEST_ASSERT_FLOAT_WITHIN(0.01f, test.expected, actual);
    }
  }
}

void testSolarChargingTable() {
  struct Case {
    bool valid;
    float current;
    bool expected;
  };
  const Case cases[] = {
      {false, 1.0f, false}, {true, NAN, false},
      {true, INFINITY, false}, {true, -0.1f, false},
      {true, 0.0f, false}, {true, 0.05f, false},
      {true, 0.0501f, true},
  };
  for (const Case& test : cases) {
    TEST_ASSERT_EQUAL(
        test.expected, logger_core::solarCharging(test.valid, test.current));
  }
}

void testPeriodicPollPolicyInitialNotDueDueAndRollover() {
  logger_core::PeriodicPollState state;
  constexpr uint32_t intervalMs = 10000U;

  TEST_ASSERT_TRUE(logger_core::periodicPollDue(1234U, state, intervalMs));
  logger_core::recordPeriodicPoll(state, 1234U);
  TEST_ASSERT_FALSE(
      logger_core::periodicPollDue(11233U, state, intervalMs));
  TEST_ASSERT_TRUE(
      logger_core::periodicPollDue(11234U, state, intervalMs));

  logger_core::recordPeriodicPoll(state, UINT32_MAX - 4000U);
  TEST_ASSERT_FALSE(
      logger_core::periodicPollDue(4998U, state, intervalMs));
  TEST_ASSERT_TRUE(
      logger_core::periodicPollDue(5999U, state, intervalMs));
}

void testPowerSnapshotCopyKeepsManagerAndProbeValuesDistinct() {
  logger_core::PowerSnapshot manager;
  manager.batteryOutput.present = true;
  manager.batteryOutput.valid = true;
  manager.batteryOutput.busVoltageV = 13.1f;
  manager.solarInput.present = true;
  manager.solarInput.valid = true;
  manager.solarInput.currentA = 1.25f;

  logger_core::PowerMonitorSnapshot probeBattery;
  logger_core::PowerMonitorSnapshot probeSolar;
  logger_core::copyPowerSnapshot(manager, probeBattery, probeSolar);

  TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.1f, probeBattery.busVoltageV);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.25f, probeSolar.currentA);
  probeBattery.busVoltageV = 12.0f;
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 13.1f, manager.batteryOutput.busVoltageV);
}

void testModbusCrcNullEmptyAndKnownFrame() {
  const uint8_t request[] = {0x01, 0x04, 0x00, 0x00, 0x00, 0x01};
  TEST_ASSERT_EQUAL_HEX16(
      0xCA31, logger_core::modbusCrc16(request, sizeof(request)));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, logger_core::modbusCrc16(nullptr, 0));
  TEST_ASSERT_EQUAL_HEX16(0, logger_core::modbusCrc16(nullptr, 1));
}

void testModbusValidationRejectsMalformedFrames() {
  uint8_t valid[] = {0x01, 0x04, 0x04, 0x41, 0x48, 0x00, 0x00, 0x00, 0x00};
  finalizeModbusFrame(valid, sizeof(valid));
  TEST_ASSERT_TRUE(logger_core::isValidModbusResponse(
      valid, sizeof(valid), 1, 0x04, 4));

  TEST_ASSERT_FALSE(
      logger_core::isValidModbusResponse(nullptr, sizeof(valid), 1, 0x04, 4));
  TEST_ASSERT_FALSE(
      logger_core::isValidModbusResponse(valid, 0, 1, 0x04, 4));
  TEST_ASSERT_FALSE(
      logger_core::isValidModbusResponse(valid, 4, 1, 0x04, 4));
  TEST_ASSERT_FALSE(logger_core::isValidModbusResponse(
      valid, sizeof(valid) - 1, 1, 0x04, 4));

  uint8_t longFrame[sizeof(valid) + 1] = {};
  for (size_t i = 0; i < sizeof(valid); ++i) longFrame[i] = valid[i];
  TEST_ASSERT_FALSE(logger_core::isValidModbusResponse(
      longFrame, sizeof(longFrame), 1, 0x04, 4));

  struct Mutation {
    size_t index;
    uint8_t value;
  };
  const Mutation mutations[] = {{0, 2}, {1, 3}, {2, 2}, {4, 0x49}};
  for (const Mutation& mutation : mutations) {
    uint8_t frame[sizeof(valid)];
    for (size_t i = 0; i < sizeof(valid); ++i) frame[i] = valid[i];
    frame[mutation.index] = mutation.value;
    TEST_ASSERT_FALSE(logger_core::isValidModbusResponse(
        frame, sizeof(frame), 1, 0x04, 4));
  }

  uint8_t exception[] = {0x01, 0x84, 0x02, 0x00, 0x00};
  finalizeModbusFrame(exception, sizeof(exception));
  TEST_ASSERT_FALSE(logger_core::isValidModbusResponse(
      exception, sizeof(exception), 1, 0x04, 0));
}

void testModbusScannerNoCandidateAndFinalOffset() {
  uint8_t response[] = {0x01, 0x04, 0x02, 0x12, 0x34, 0x00, 0x00};
  finalizeModbusFrame(response, sizeof(response));

  TEST_ASSERT_EQUAL_INT(
      -1, logger_core::findValidModbusResponse(nullptr, sizeof(response),
                                                1, 0x04, 2));
  TEST_ASSERT_EQUAL_INT(
      -1, logger_core::findValidModbusResponse(response, sizeof(response) - 1,
                                                1, 0x04, 2));

  uint8_t noCandidate[sizeof(response) + 3] = {};
  for (size_t i = 0; i < sizeof(response); ++i) {
    noCandidate[i + 3] = response[i];
  }

  noCandidate[sizeof(noCandidate) - 1] ^= 1;
  TEST_ASSERT_EQUAL_INT(
      -1, logger_core::findValidModbusResponse(
              noCandidate, sizeof(noCandidate), 1, 0x04, 2));

  uint8_t finalCandidate[sizeof(response) + 4] = {0xAA, 0xBB, 0xCC, 0xDD};
  for (size_t i = 0; i < sizeof(response); ++i) {
    finalCandidate[i + 4] = response[i];
  }
  TEST_ASSERT_EQUAL_INT(
      4, logger_core::findValidModbusResponse(
             finalCandidate, sizeof(finalCandidate), 1, 0x04, 2));
}

void testModbusReadRequestGoldenVectorsAndBounds() {
  uint8_t request[logger_core::MODBUS_READ_REQUEST_BYTES] = {};
  TEST_ASSERT_EQUAL_UINT32(
      sizeof(request), logger_core::buildModbusReadRequest(
                           1, 0x04, 0x0006, 4, request, sizeof(request)));
  const uint8_t solinstExpected[] =
      {0x01, 0x04, 0x00, 0x06, 0x00, 0x04, 0x11, 0xC8};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(solinstExpected, request, sizeof(request));

  TEST_ASSERT_EQUAL_UINT32(
      sizeof(request), logger_core::buildModbusReadRequest(
                           2, 0x03, 0x01F4, 1, request, sizeof(request)));
  const uint8_t weatherExpected[] =
      {0x02, 0x03, 0x01, 0xF4, 0x00, 0x01, 0xC4, 0x37};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(weatherExpected, request, sizeof(request));

  TEST_ASSERT_EQUAL_UINT32(
      0, logger_core::buildModbusReadRequest(
             0, 0x04, 0, 1, request, sizeof(request)));
  TEST_ASSERT_EQUAL_UINT32(
      0, logger_core::buildModbusReadRequest(
             248, 0x04, 0, 1, request, sizeof(request)));
  TEST_ASSERT_EQUAL_UINT32(
      0, logger_core::buildModbusReadRequest(
             1, 0x06, 0, 1, request, sizeof(request)));
  TEST_ASSERT_EQUAL_UINT32(
      0, logger_core::buildModbusReadRequest(
             1, 0x04, 0, 0, request, sizeof(request)));
  TEST_ASSERT_EQUAL_UINT32(
      0, logger_core::buildModbusReadRequest(
             1, 0x04, 0, 126, request, sizeof(request)));
  TEST_ASSERT_EQUAL_UINT32(
      0, logger_core::buildModbusReadRequest(
             1, 0x04, 0, 1, nullptr, sizeof(request)));
  TEST_ASSERT_EQUAL_UINT32(
      0, logger_core::buildModbusReadRequest(
             1, 0x04, 0, 1, request, sizeof(request) - 1));
}

void testModbusRegisterDecodeHandlesEchoNoiseAndMalformedFrames() {
  uint8_t frame[] =
      {0x01, 0x04, 0x04, 0x12, 0x34, 0xAB, 0xCD, 0x00, 0x00};
  finalizeModbusFrame(frame, sizeof(frame));
  uint8_t stream[20] = {0xAA, 0x55, 0x01, 0x04, 0x00, 0x06,
                        0x00, 0x02, 0x90, 0x00, 0xFF};
  memcpy(stream + 11, frame, sizeof(frame));

  uint16_t values[2] = {};
  size_t offset = 99;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::ModbusDecodeResult::OK),
      static_cast<int>(logger_core::decodeModbusReadRegisters(
          stream, sizeof(stream), 1, 0x04, 2, values, 2, &offset)));
  TEST_ASSERT_EQUAL_UINT32(11, offset);
  TEST_ASSERT_EQUAL_HEX16(0x1234, values[0]);
  TEST_ASSERT_EQUAL_HEX16(0xABCD, values[1]);

  stream[sizeof(stream) - 1] ^= 1;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::ModbusDecodeResult::FRAME_NOT_FOUND),
      static_cast<int>(logger_core::decodeModbusReadRegisters(
          stream, sizeof(stream), 1, 0x04, 2, values, 2)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::ModbusDecodeResult::INVALID_ARGUMENT),
      static_cast<int>(logger_core::decodeModbusReadRegisters(
          frame, sizeof(frame), 1, 0x04, 2, values, 1)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::ModbusDecodeResult::INVALID_ARGUMENT),
      static_cast<int>(logger_core::decodeModbusReadRegisters(
          nullptr, 0, 1, 0x04, 2, values, 2)));

  uint8_t weatherFrame[] =
      {0x02, 0x03, 0x02, 0x00, 0x2A, 0x00, 0x00};
  finalizeModbusFrame(weatherFrame, sizeof(weatherFrame));
  uint16_t windSpeed = 0;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::ModbusDecodeResult::OK),
      static_cast<int>(logger_core::decodeModbusReadRegisters(
          weatherFrame, sizeof(weatherFrame), 2, 0x03, 1, &windSpeed, 1)));
  TEST_ASSERT_EQUAL_UINT16(42, windSpeed);
}

void testExponentialBackoffTable() {
  struct Case {
    uint32_t initial;
    uint32_t maximum;
    uint32_t failures;
    uint32_t expected;
  };
  const Case cases[] = {
      {30000U, 900000U, 0U, 0U},
      {30000U, 900000U, 1U, 30000U},
      {30000U, 900000U, 2U, 60000U},
      {30000U, 900000U, 5U, 480000U},
      {30000U, 900000U, 6U, 900000U},
      {30000U, 900000U, 100U, 900000U},
      {5000U, 1000U, 1U, 1000U},
      {0U, 1000U, 5U, 0U},
      {1U, UINT32_MAX, 32U, 0x80000000U},
      {1U, UINT32_MAX, 33U, UINT32_MAX},
  };
  for (const Case& test : cases) {
    TEST_ASSERT_EQUAL_UINT32(
        test.expected, logger_core::exponentialBackoff(
                           test.initial, test.maximum, test.failures));
  }
}

void testDeadlineComparisonTableIncludingRollover() {
  struct Case {
    uint32_t now;
    uint32_t deadline;
    bool pending;
  };
  const Case cases[] = {
      {100U, 101U, true},
      {101U, 101U, false},
      {102U, 101U, false},
      {0xFFFFFFF0U, 0x20U, true},
      {0x20U, 0x20U, false},
      {0x21U, 0x20U, false},
      {0xFFFFFFFEU, 1U, true},
      {1U, 0xFFFFFFFEU, false},
  };
  for (const Case& test : cases) {
    TEST_ASSERT_EQUAL(
        test.pending, logger_core::deadlinePending(test.now, test.deadline));
  }
}

void testNtpPolicyFirstAttemptAndSuccessInterval() {
  logger_core::NtpSyncState state;
  TEST_ASSERT_TRUE(logger_core::ntpSyncDue(1234U, state));

  logger_core::recordNtpSyncResult(
      state, 1000U, true, true, 6000U, 30U, 300U, 300U, 3000U);
  TEST_ASSERT_TRUE(state.attempted);
  TEST_ASSERT_TRUE(state.lastAttemptSucceeded);
  TEST_ASSERT_EQUAL_UINT32(0, state.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(7000U, state.nextAttemptMs);
  TEST_ASSERT_FALSE(logger_core::ntpSyncDue(6999U, state));
  TEST_ASSERT_TRUE(logger_core::ntpSyncDue(7000U, state));
}

void testNtpPolicyFailureRetryForValidAndInvalidClocks() {
  logger_core::NtpSyncState invalidClock;
  logger_core::recordNtpSyncResult(
      invalidClock, 1000U, false, false, 6000U, 30U, 300U, 300U, 3000U);
  TEST_ASSERT_FALSE(invalidClock.lastAttemptSucceeded);
  TEST_ASSERT_EQUAL_UINT32(1, invalidClock.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1030U, invalidClock.nextAttemptMs);
  TEST_ASSERT_FALSE(logger_core::ntpSyncDue(1029U, invalidClock));
  TEST_ASSERT_TRUE(logger_core::ntpSyncDue(1030U, invalidClock));

  logger_core::recordNtpSyncResult(
      invalidClock, 1030U, false, false, 6000U, 30U, 300U, 300U, 3000U);
  TEST_ASSERT_EQUAL_UINT32(2, invalidClock.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1090U, invalidClock.nextAttemptMs);

  logger_core::NtpSyncState validClock;
  logger_core::recordNtpSyncResult(
      validClock, 1000U, false, true, 6000U, 30U, 300U, 300U, 3000U);
  TEST_ASSERT_EQUAL_UINT32(1300U, validClock.nextAttemptMs);
  TEST_ASSERT_FALSE(logger_core::ntpSyncDue(1299U, validClock));
  TEST_ASSERT_TRUE(logger_core::ntpSyncDue(1300U, validClock));
  TEST_ASSERT_EQUAL_UINT32(
      300U, logger_core::ntpCooldownRemaining(1000U, validClock));
}

void testNtpPolicyBoundedBackoffAndMillisRollover() {
  logger_core::NtpSyncState state;
  state.consecutiveFailures = 20;
  logger_core::recordNtpSyncResult(
      state, 0xFFFFFFF0U, false, false, 6000U, 30U, 300U, 300U, 3000U);
  TEST_ASSERT_EQUAL_UINT32(0x0000011CU, state.nextAttemptMs);
  TEST_ASSERT_EQUAL_UINT32(
      300U, logger_core::ntpCooldownRemaining(0xFFFFFFF0U, state));
  TEST_ASSERT_FALSE(logger_core::ntpSyncDue(0x0000011BU, state));
  TEST_ASSERT_TRUE(logger_core::ntpSyncDue(0x0000011CU, state));

  logger_core::recordNtpSyncResult(
      state, 0x0000011CU, true, true, 6000U, 30U, 300U, 300U, 3000U);
  TEST_ASSERT_EQUAL_UINT32(0, state.consecutiveFailures);
  TEST_ASSERT_FALSE(logger_core::ntpSyncDue(0x0000188BU, state));
  TEST_ASSERT_TRUE(logger_core::ntpSyncDue(0x0000188CU, state));
}

void testSensorDiscoveryPolicyTable() {
  struct Case {
    const char* name;
    uint32_t now;
    bool known;
    logger_core::SensorDiscoveryState state;
    bool due;
    uint32_t cooldown;
  };

  logger_core::SensorDiscoveryState boot;
  logger_core::SensorDiscoveryState failedBoot;
  logger_core::recordSensorDiscoveryResult(
      failedBoot, 1000U, false, 30000U, 900000U);
  logger_core::SensorDiscoveryState discoveredBoot;
  logger_core::recordSensorDiscoveryResult(
      discoveredBoot, 1000U, true, 30000U, 900000U);
  logger_core::SensorDiscoveryState rollover;
  logger_core::recordSensorDiscoveryResult(
      rollover, 0xFFFFFFF0U, false, 30000U, 900000U);

  const Case cases[] = {
      {"boot attempt due", 1000U, false, boot, true, 0U},
      {"boot failure not due", 30999U, false, failedBoot, false, 1U},
      {"boot failure due", 31000U, false, failedBoot, true, 0U},
      {"boot success suppressed", 50000U, true, discoveredBoot, false, 0U},
      {"known suppresses failed state", 31000U, true, failedBoot, false, 0U},
      {"rollover not due", 0x0000751FU, false, rollover, false, 1U},
      {"rollover due", 0x00007520U, false, rollover, true, 0U},
  };

  for (const Case& test : cases) {
    (void)test.name;
    TEST_ASSERT_EQUAL(
        test.due, logger_core::sensorDiscoveryDue(
                      test.now, test.known, test.state));
    TEST_ASSERT_EQUAL_UINT32(
        test.cooldown, logger_core::sensorDiscoveryCooldownRemaining(
                           test.now, test.known, test.state));
  }

  TEST_ASSERT_TRUE(failedBoot.attempted);
  TEST_ASSERT_FALSE(failedBoot.lastAttemptSucceeded);
  TEST_ASSERT_EQUAL_UINT32(1U, failedBoot.attemptCount);
  TEST_ASSERT_EQUAL_UINT32(1U, failedBoot.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(31000U, failedBoot.nextAttemptMs);

  logger_core::SensorDiscoveryState maxBackoff;
  maxBackoff.consecutiveFailures = 20U;
  logger_core::recordSensorDiscoveryResult(
      maxBackoff, 1000U, false, 30000U, 900000U);
  TEST_ASSERT_EQUAL_UINT32(901000U, maxBackoff.nextAttemptMs);

  logger_core::recordSensorDiscoveryResult(
      maxBackoff, 901000U, true, 30000U, 900000U);
  TEST_ASSERT_TRUE(maxBackoff.lastAttemptSucceeded);
  TEST_ASSERT_EQUAL_UINT32(0U, maxBackoff.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(0U, logger_core::sensorDiscoveryCooldownRemaining(
                                  901000U, true, maxBackoff));
}

void testHttpServerStartTransitionPolicy() {
  bool started = false;
  TEST_ASSERT_FALSE(logger_core::shouldStartHttpServer(false, started));

  TEST_ASSERT_TRUE(logger_core::shouldStartHttpServer(true, started));
  started = true;
  TEST_ASSERT_FALSE(logger_core::shouldStartHttpServer(true, started));

  TEST_ASSERT_FALSE(logger_core::shouldStartHttpServer(false, started));
  TEST_ASSERT_FALSE(logger_core::shouldStartHttpServer(true, started));
}

void testUploadStatusClassificationTable() {
  using logger_core::UploadEndpointMode;
  using logger_core::UploadOutcome;
  struct Case {
    int status;
    UploadEndpointMode mode;
    UploadOutcome expected;
  };
  const Case cases[] = {
      {-5, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {0, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {199, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {200, UploadEndpointMode::FESLABS_INGEST, UploadOutcome::ACCEPTED},
      {204, UploadEndpointMode::FESLABS_INGEST, UploadOutcome::ACCEPTED},
      {299, UploadEndpointMode::FESLABS_INGEST, UploadOutcome::ACCEPTED},
      {301, UploadEndpointMode::GOOGLE_APPS_SCRIPT, UploadOutcome::ACCEPTED},
      {302, UploadEndpointMode::GOOGLE_APPS_SCRIPT, UploadOutcome::ACCEPTED},
      {303, UploadEndpointMode::GOOGLE_APPS_SCRIPT, UploadOutcome::ACCEPTED},
      {307, UploadEndpointMode::GOOGLE_APPS_SCRIPT, UploadOutcome::ACCEPTED},
      {308, UploadEndpointMode::GOOGLE_APPS_SCRIPT, UploadOutcome::ACCEPTED},
      {300, UploadEndpointMode::GOOGLE_APPS_SCRIPT,
       UploadOutcome::TRANSIENT_FAILURE},
      {304, UploadEndpointMode::GOOGLE_APPS_SCRIPT,
       UploadOutcome::TRANSIENT_FAILURE},
      {306, UploadEndpointMode::GOOGLE_APPS_SCRIPT,
       UploadOutcome::TRANSIENT_FAILURE},
      {301, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {302, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {303, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {307, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {308, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {400, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {401, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {403, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {404, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {408, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {409, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {413, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {415, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {418, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {422, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {425, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {429, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {499, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::PERMANENT_REJECTION},
      {500, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {503, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
      {599, UploadEndpointMode::FESLABS_INGEST,
       UploadOutcome::TRANSIENT_FAILURE},
  };

  for (const Case& test : cases) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(test.expected),
        static_cast<int>(
            logger_core::classifyUploadStatus(test.status, test.mode)));
  }
}

void testUploadRetryAndReadingActionsTable() {
  using logger_core::BacklogUploadAction;
  using logger_core::FreshUploadAction;
  using logger_core::UploadOutcome;
  struct Case {
    UploadOutcome outcome;
    bool retryBeforeLimit;
    FreshUploadAction fresh;
    BacklogUploadAction backlog;
  };
  const Case cases[] = {
      {UploadOutcome::ACCEPTED, false, FreshUploadAction::COMPLETE,
       BacklogUploadAction::DEQUEUE},
      {UploadOutcome::TRANSIENT_FAILURE, true, FreshUploadAction::ENQUEUE,
       BacklogUploadAction::RETAIN},
      {UploadOutcome::DEFERRED, false, FreshUploadAction::ENQUEUE,
       BacklogUploadAction::RETAIN},
      {UploadOutcome::PERMANENT_REJECTION, false, FreshUploadAction::DROP,
       BacklogUploadAction::DEQUEUE_REJECTED},
  };

  for (const Case& test : cases) {
    TEST_ASSERT_EQUAL(
        test.retryBeforeLimit,
        logger_core::shouldRetryUpload(test.outcome, 1, 3));
    TEST_ASSERT_FALSE(logger_core::shouldRetryUpload(test.outcome, 3, 3));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(test.fresh),
        static_cast<int>(logger_core::freshUploadAction(test.outcome)));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(test.backlog),
        static_cast<int>(logger_core::backlogUploadAction(test.outcome)));
  }
}

void testUploadCooldownRecordedOncePerLogicalOperation() {
  using logger_core::UploadOutcome;
  logger_core::UploadCooldownState state;

  // Three wire attempts are one operation: retry decisions do not mutate
  // cooldown state, and the final operation result records one failure.
  TEST_ASSERT_TRUE(
      logger_core::shouldRetryUpload(UploadOutcome::TRANSIENT_FAILURE, 1, 3));
  TEST_ASSERT_TRUE(
      logger_core::shouldRetryUpload(UploadOutcome::TRANSIENT_FAILURE, 2, 3));
  TEST_ASSERT_FALSE(
      logger_core::shouldRetryUpload(UploadOutcome::TRANSIENT_FAILURE, 3, 3));
  TEST_ASSERT_EQUAL_UINT32(0, state.consecutiveFailures);

  logger_core::recordUploadOperationOutcome(
      state, 1000U, UploadOutcome::TRANSIENT_FAILURE, 30U, 300U);
  TEST_ASSERT_EQUAL_UINT32(1, state.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1030U, state.nextAllowedMs);

  logger_core::recordUploadOperationOutcome(
      state, 1010U, UploadOutcome::DEFERRED, 30U, 300U);
  TEST_ASSERT_EQUAL_UINT32(1, state.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1030U, state.nextAllowedMs);

  logger_core::recordUploadOperationOutcome(
      state, 1030U, UploadOutcome::TRANSIENT_FAILURE, 30U, 300U);
  TEST_ASSERT_EQUAL_UINT32(2, state.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(1090U, state.nextAllowedMs);

  logger_core::recordUploadOperationOutcome(
      state, 1090U, UploadOutcome::PERMANENT_REJECTION, 30U, 300U);
  TEST_ASSERT_EQUAL_UINT32(0, state.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(0, state.nextAllowedMs);

  state.consecutiveFailures = 2;
  state.nextAllowedMs = 2000;
  logger_core::recordUploadOperationOutcome(
      state, 1500U, UploadOutcome::ACCEPTED, 30U, 300U);
  TEST_ASSERT_EQUAL_UINT32(0, state.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(0, state.nextAllowedMs);
}

logger_core::WeatherSample weatherSample(
    float temperature, float humidity, float pressure, float windSpeed,
    float windDirection, float rain, float light) {
  logger_core::WeatherSample sample;
  sample.airTemperatureC = temperature;
  sample.relativeHumidityPct = humidity;
  sample.barometricPressureHpa = pressure;
  sample.windSpeedMs = windSpeed;
  sample.windDirectionDeg = windDirection;
  sample.rainfallAccumulatedMm = rain;
  sample.lightLux = light;
  return sample;
}

void testWeatherSummaryFirstSampleAndNumericAggregation() {
  logger_core::WeatherSummary summary;
  logger_core::addWeatherSample(
      summary, weatherSample(10, 40, 1000, 2, 350, 7.5f, 100));

  TEST_ASSERT_EQUAL_UINT16(1, summary.sampleCount);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10, summary.airTemperatureSum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10, summary.airTemperatureMin);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10, summary.airTemperatureMax);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, summary.rainfallFirstMm);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 7.5f, summary.rainfallLastMm);
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 0, logger_core::rainfallIntervalMm(summary));

  logger_core::addWeatherSample(
      summary, weatherSample(14, 60, 1020, 6, 10, 8.25f, 300));
  TEST_ASSERT_EQUAL_UINT16(2, summary.sampleCount);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 24, summary.airTemperatureSum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10, summary.airTemperatureMin);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14, summary.airTemperatureMax);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 100, summary.relativeHumiditySum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 2020, summary.barometricPressureSum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 8, summary.windSpeedSum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 400, summary.lightSum);
  TEST_ASSERT_FLOAT_WITHIN(
      0.01f, 0, logger_core::averageWindDirectionDegrees(summary));
  TEST_ASSERT_FLOAT_WITHIN(
      0.001f, 0.75f, logger_core::rainfallIntervalMm(summary));
  TEST_ASSERT_FALSE(summary.rainfallCounterReset);
}

void testWeatherSummaryRainfallDecreaseMarksReset() {
  logger_core::WeatherSummary empty;
  TEST_ASSERT_TRUE(
      std::isnan(logger_core::averageWindDirectionDegrees(empty)));
  TEST_ASSERT_TRUE(std::isnan(logger_core::rainfallIntervalMm(empty)));

  logger_core::addWeatherSample(
      empty, weatherSample(1, 2, 3, 4, 90, 10, 5));
  logger_core::addWeatherSample(
      empty, weatherSample(1, 2, 3, 4, 90, 1, 5));
  TEST_ASSERT_TRUE(empty.rainfallCounterReset);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1, empty.rainfallLastMm);
  TEST_ASSERT_TRUE(std::isnan(logger_core::rainfallIntervalMm(empty)));
}

void testSensorIdentityValidityTable() {
  struct Case {
    uint32_t serial;
    uint8_t major;
    uint8_t minor;
    uint16_t beta;
    bool valid;
  };
  const Case cases[] = {
      {0, 1, 0, 0, false},
      {UINT32_MAX, 1, 0, 0, false},
      {1, 0, 0, 0, false},
      {1, 0, 0, 7, false},
      {1, 1, 0, 0, true},
      {1, 0, 1, 0, true},
      {0, 0, 0, 0, false},
      {UINT32_MAX, 0, 0, 0, false},
  };
  for (const Case& test : cases) {
    TEST_ASSERT_EQUAL(
        test.valid, logger_core::sensorIdentityValid(
                        test.serial, test.major, test.minor, test.beta));
  }
}

void assertLogDecision(logger_core::LogScheduleDecision expected,
                       bool clockValid, int64_t epochSeconds, int interval,
                       int window, logger_core::LogScheduleState& state) {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(expected),
      static_cast<int>(logger_core::observeUtcLogSchedule(
          clockValid, epochSeconds, interval, window, state)));
}

void testUtcScheduleBootAtBoundaryAndRepeatedCalls() {
  logger_core::LogScheduleState state;
  assertLogDecision(logger_core::LogScheduleDecision::BOUNDARY, true, 3600, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3600, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3604, 60,
                    5, state);
  TEST_ASSERT_EQUAL_INT64(1, state.latestIntervalKey);
  TEST_ASSERT_EQUAL_UINT32(0, state.catchUpCount);

  logger_core::LogScheduleState insideWindow;
  assertLogDecision(logger_core::LogScheduleDecision::BOUNDARY, true, 3604, 60,
                    5, insideWindow);

  logger_core::LogScheduleState outsideWindow;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3605, 60,
                    5, outsideWindow);
}

void testUtcScheduleBootMidIntervalDoesNotFire() {
  logger_core::LogScheduleState state;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3665, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3700, 60,
                    5, state);
  TEST_ASSERT_TRUE(state.initialized);
  TEST_ASSERT_EQUAL_INT64(1, state.latestIntervalKey);
}

void testUtcScheduleDelayedCrossingCatchesUpOnce() {
  logger_core::LogScheduleState state;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 7198, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::CATCH_UP, true, 7260, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 7261, 60,
                    5, state);
  TEST_ASSERT_EQUAL_UINT32(1, state.catchUpCount);
}

void testUtcScheduleForwardJumpProducesSingleCurrentCatchUp() {
  logger_core::LogScheduleState state;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3700, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::CATCH_UP, true, 18090, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 18091, 60,
                    5, state);
  TEST_ASSERT_EQUAL_INT64(5, state.latestIntervalKey);
  TEST_ASSERT_EQUAL_UINT32(1, state.catchUpCount);
}

void testUtcScheduleBackwardCorrectionAndRecovery() {
  logger_core::LogScheduleState state;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 10830, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::BOUNDARY, true, 14402, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 7210, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 14403, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::CATCH_UP, true, 18020, 60,
                    5, state);
  TEST_ASSERT_EQUAL_INT64(5, state.latestIntervalKey);
}

void testUtcScheduleDayAndYearRollover() {
  logger_core::LogScheduleState day;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true,
                    1798761598LL, 60, 5, day);
  assertLogDecision(logger_core::LogScheduleDecision::BOUNDARY, true,
                    1798761603LL, 60, 5, day);

  logger_core::LogScheduleState year;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true,
                    1798761598LL, 1440, 5, year);
  assertLogDecision(logger_core::LogScheduleDecision::BOUNDARY, true,
                    1798761603LL, 1440, 5, year);
}

void testUtcScheduleInvalidClockAndConfigurationDoNotMutateState() {
  logger_core::LogScheduleState state;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, false, 3600, 60,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, -1, 60, 5,
                    state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3600, 0,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3600, 60,
                    0, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 3600, 1,
                    61, state);
  TEST_ASSERT_FALSE(state.initialized);
}

void testUtcScheduleNonDivisorIntervalUsesEpochIdentity() {
  logger_core::LogScheduleState state;
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 41999, 7,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::BOUNDARY, true, 42000, 7,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::CATCH_UP, true, 42430, 7,
                    5, state);
  assertLogDecision(logger_core::LogScheduleDecision::NOT_DUE, true, 42431, 7,
                    5, state);
}

void testBoardProfilesTable() {
  struct Case {
    const BoardProfile* profile;
    const char* name;
    LoggerRole role;
    uint8_t channels;
    bool solinst;
    bool weather;
    DisplayBehavior display;
    size_t backlog;
  };
  const Case cases[] = {
      {&OPTA_SOLINST_PROFILE, "opta-solinst", LoggerRole::SOLINST_LOGGER, 1,
       true, false, DisplayBehavior::WAKE_ON_DEMAND, 32},
      {&GIGA_SITE_PROFILE, "giga-site", LoggerRole::SITE_LOGGER, 2,
       true, true, DisplayBehavior::PERSISTENT_EPAPER, 256},
  };
  for (const Case& test : cases) {
    TEST_ASSERT_EQUAL_STRING(test.name, test.profile->name);
    TEST_ASSERT_EQUAL(static_cast<int>(test.role),
                      static_cast<int>(test.profile->role));
    TEST_ASSERT_EQUAL_UINT8(test.channels, test.profile->rs485ChannelCount);
    TEST_ASSERT_EQUAL(test.solinst, test.profile->solinstEnabled);
    TEST_ASSERT_EQUAL(test.weather, test.profile->weatherEnabled);
    TEST_ASSERT_EQUAL(static_cast<int>(test.display),
                      static_cast<int>(test.profile->displayBehavior));
    TEST_ASSERT_EQUAL_UINT32(test.backlog, test.profile->backlogCapacity);
  }
#if defined(LOGGER_BOARD_GIGA)
  TEST_ASSERT_EQUAL_STRING(GIGA_SITE_PROFILE.name, ACTIVE_BOARD_PROFILE.name);
#else
  TEST_ASSERT_EQUAL_STRING(OPTA_SOLINST_PROFILE.name, ACTIVE_BOARD_PROFILE.name);
#endif
}

void testSiteHealthClassification() {
  logger_core::SiteSnapshot snapshot;
  snapshot.clockValid = true;
  snapshot.wifiConnected = true;
  snapshot.sensorFound = true;
  snapshot.waterValid = true;
  snapshot.batteryValid = true;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::SiteHealth::HEALTHY),
      static_cast<int>(logger_core::classifySiteHealth(snapshot)));

  snapshot.wifiConnected = false;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::SiteHealth::DEGRADED),
      static_cast<int>(logger_core::classifySiteHealth(snapshot)));
  snapshot.wifiConnected = true;
  snapshot.weatherEnabled = true;
  snapshot.weatherValid = false;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::SiteHealth::DEGRADED),
      static_cast<int>(logger_core::classifySiteHealth(snapshot)));
  snapshot.sensorFound = false;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::SiteHealth::CRITICAL),
      static_cast<int>(logger_core::classifySiteHealth(snapshot)));
  TEST_ASSERT_EQUAL_STRING(
      "critical", logger_core::siteHealthName(
                      logger_core::SiteHealth::CRITICAL));
}

void testEpaperRefreshPolicyLifecycleAndRollover() {
  logger_core::SiteSnapshot snapshot;
  snapshot.health = logger_core::SiteHealth::HEALTHY;
  snapshot.readingRevision = 1;
  logger_core::EpaperRefreshState state;

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::FULL),
      static_cast<int>(logger_core::observeEpaperRefresh(
          1000, snapshot, 900000, 86400000, state)));
  snapshot.readingRevision = 2;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::PARTIAL),
      static_cast<int>(logger_core::observeEpaperRefresh(
          2000, snapshot, 900000, 86400000, state)));
  snapshot.health = logger_core::SiteHealth::DEGRADED;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::PARTIAL),
      static_cast<int>(logger_core::observeEpaperRefresh(
          2001, snapshot, 900000, 86400000, state)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::PARTIAL),
      static_cast<int>(logger_core::observeEpaperRefresh(
          902001, snapshot, 900000, 86400000, state)));

  state.lastFullRefreshMs = UINT32_MAX - 99;
  state.lastRefreshMs = UINT32_MAX - 99;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::FULL),
      static_cast<int>(logger_core::observeEpaperRefresh(
          86399900, snapshot, 900000, 86400000, state)));
}

void testEpaperRefreshDecisionCommitsOnlyAfterSuccessfulRender() {
  logger_core::SiteSnapshot snapshot;
  snapshot.health = logger_core::SiteHealth::HEALTHY;
  snapshot.readingRevision = 7;
  logger_core::EpaperRefreshState state;

  const logger_core::EpaperRefreshDecision firstDecision =
      logger_core::decideEpaperRefresh(
          1000, snapshot, 900000, 86400000, state);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::FULL),
      static_cast<int>(firstDecision));
  TEST_ASSERT_FALSE(state.initialized);

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::FULL),
      static_cast<int>(logger_core::decideEpaperRefresh(
          61000, snapshot, 900000, 86400000, state)));
  logger_core::recordEpaperRefresh(61000, snapshot, firstDecision, state);
  TEST_ASSERT_TRUE(state.initialized);
  TEST_ASSERT_EQUAL_UINT32(61000, state.lastRefreshMs);
  TEST_ASSERT_EQUAL_UINT32(61000, state.lastFullRefreshMs);
  TEST_ASSERT_EQUAL_UINT32(7, state.lastReadingRevision);
}

void testSc16is752CommandsBaudDivisorsAndFraming() {
  TEST_ASSERT_EQUAL_HEX8(
      0x00, logger_core::sc16is752Command(0, 0, false));
  TEST_ASSERT_EQUAL_HEX8(
      0x82, logger_core::sc16is752Command(1, 0, true));
  TEST_ASSERT_EQUAL_HEX8(
      0xBA, logger_core::sc16is752Command(1, 7, true));

  uint16_t divisor = 0;
  TEST_ASSERT_TRUE(
      logger_core::sc16is752BaudDivisor(14745600, 19200, divisor));
  TEST_ASSERT_EQUAL_UINT16(48, divisor);
  TEST_ASSERT_TRUE(
      logger_core::sc16is752BaudDivisor(14745600, 115200, divisor));
  TEST_ASSERT_EQUAL_UINT16(8, divisor);
  TEST_ASSERT_FALSE(
      logger_core::sc16is752BaudDivisor(14745600, 0, divisor));
  TEST_ASSERT_EQUAL_HEX8(
      0x03, logger_core::sc16is752LineControl(false));
  TEST_ASSERT_EQUAL_HEX8(
      0x1B, logger_core::sc16is752LineControl(true));
}

void testRollingExtremaBucketsMergeExpireAndRejectInvalidValues() {
  logger_core::RollingExtrema<4, 10> history;
  float minimum = 0;
  float maximum = 0;
  TEST_ASSERT_FALSE(history.extrema(0, minimum, maximum));
  TEST_ASSERT_FALSE(history.add(-1, 12.0f));
  TEST_ASSERT_FALSE(history.add(0, NAN));

  TEST_ASSERT_TRUE(history.add(1, 12.5f));
  TEST_ASSERT_TRUE(history.add(9, 11.5f));
  TEST_ASSERT_TRUE(history.add(10, 13.0f));
  TEST_ASSERT_TRUE(history.add(29, 10.0f));
  TEST_ASSERT_TRUE(history.extrema(30, minimum, maximum));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, minimum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 13.0f, maximum);

  TEST_ASSERT_TRUE(history.add(40, 14.0f));
  TEST_ASSERT_TRUE(history.extrema(40, minimum, maximum));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, minimum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.0f, maximum);
  TEST_ASSERT_TRUE(history.extrema(70, minimum, maximum));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.0f, minimum);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 14.0f, maximum);
  TEST_ASSERT_FALSE(history.extrema(80, minimum, maximum));

  history.clear();
  TEST_ASSERT_FALSE(history.extrema(40, minimum, maximum));
}

void testEpaperPolicyRejectsInvalidConfigurationAndBoundsBusyWait() {
  logger_core::SiteSnapshot snapshot;
  logger_core::EpaperRefreshState state;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(logger_core::EpaperRefreshDecision::NONE),
      static_cast<int>(
          logger_core::observeEpaperRefresh(1, snapshot, 0, 100, state)));
  TEST_ASSERT_FALSE(state.initialized);
  TEST_ASSERT_FALSE(logger_core::epaperBusyTimedOut(1099, 1000, 100));
  TEST_ASSERT_TRUE(logger_core::epaperBusyTimedOut(1100, 1000, 100));
  TEST_ASSERT_TRUE(
      logger_core::epaperBusyTimedOut(50, UINT32_MAX - 49, 100));
  TEST_ASSERT_TRUE(logger_core::epaperBusyTimedOut(1000, 1000, 0));
}

void testHttpRequestRoutesAreExactAndQueriesAreNotAccepted() {
  struct Case {
    const char* line;
    logger_core::HttpMethod method;
    logger_core::HttpRoute route;
    logger_core::HttpRouteDecision decision;
    bool hasQuery;
  };
  const Case cases[] = {
      {"GET / HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::INDEX, logger_core::HttpRouteDecision::INDEX,
       false},
      {"GET /status HTTP/1.0", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::STATUS, logger_core::HttpRouteDecision::STATUS,
       false},
      {"GET /probe HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::PROBE, logger_core::HttpRouteDecision::PROBE,
       false},
      {"GET /reset HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::RESET, logger_core::HttpRouteDecision::RESET,
       false},
      {"GET /reset-anything HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::UNKNOWN,
       logger_core::HttpRouteDecision::NOT_FOUND, false},
      {"GET /probe-x HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::UNKNOWN,
       logger_core::HttpRouteDecision::NOT_FOUND, false},
      {"GET /status/extra HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::UNKNOWN,
       logger_core::HttpRouteDecision::NOT_FOUND, false},
      {"GET /status?fresh=1 HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::UNKNOWN,
       logger_core::HttpRouteDecision::NOT_FOUND, true},
      {"GET /unknown?path=/reset HTTP/1.1", logger_core::HttpMethod::GET,
       logger_core::HttpRoute::UNKNOWN,
       logger_core::HttpRouteDecision::NOT_FOUND, true},
      {"POST /reset HTTP/1.1", logger_core::HttpMethod::OTHER,
       logger_core::HttpRoute::RESET,
       logger_core::HttpRouteDecision::METHOD_NOT_ALLOWED, false},
  };

  for (const Case& test : cases) {
    logger_core::HttpRequest request;
    TEST_ASSERT_EQUAL(
        static_cast<int>(logger_core::HttpRequestParseResult::OK),
        static_cast<int>(logger_core::parseHttpRequestLine(
            test.line, strlen(test.line), request)));
    TEST_ASSERT_EQUAL(static_cast<int>(test.method),
                      static_cast<int>(request.method));
    TEST_ASSERT_EQUAL(static_cast<int>(test.route),
                      static_cast<int>(request.route));
    TEST_ASSERT_EQUAL(
        static_cast<int>(test.decision),
        static_cast<int>(logger_core::routeHttpRequest(request)));
    TEST_ASSERT_EQUAL(test.hasQuery, request.hasQuery);
  }
}

void testHttpRequestRejectsMalformedLines() {
  const char* cases[] = {
      "", " /status HTTP/1.1", "G@T /status HTTP/1.1",
      "GET  /status HTTP/1.1", "GET /status  HTTP/1.1",
      "GET /status", "GET status HTTP/1.1", "GET /status HTTP/2",
      "GET /status HTTP/1.1 extra", "GET /status#fragment HTTP/1.1",
      "GET ?query HTTP/1.1",
  };
  for (const char* line : cases) {
    logger_core::HttpRequest request;
    TEST_ASSERT_EQUAL(
        static_cast<int>(logger_core::HttpRequestParseResult::BAD_REQUEST),
        static_cast<int>(logger_core::parseHttpRequestLine(
            line, strlen(line), request)));
  }
}

void testHttpRequestLineCrlfAndConfiguredLimits() {
  logger_core::RequestLineReader exact;
  for (size_t i = 0; i < logger_core::HTTP_MAX_REQUEST_LINE_BYTES; ++i) {
    TEST_ASSERT_EQUAL(
        static_cast<int>(logger_core::RequestLineReadResult::IN_PROGRESS),
        static_cast<int>(
            logger_core::consumeRequestLineByte(exact, 'A')));
  }
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::RequestLineReadResult::IN_PROGRESS),
      static_cast<int>(logger_core::consumeRequestLineByte(exact, '\r')));
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::RequestLineReadResult::COMPLETE),
      static_cast<int>(logger_core::consumeRequestLineByte(exact, '\n')));
  TEST_ASSERT_EQUAL_UINT32(logger_core::HTTP_MAX_REQUEST_LINE_BYTES,
                           exact.length);

  logger_core::RequestLineReader overlong;
  for (size_t i = 0; i < logger_core::HTTP_MAX_REQUEST_LINE_BYTES; ++i) {
    logger_core::consumeRequestLineByte(overlong, 'A');
  }
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::RequestLineReadResult::TOO_LONG),
      static_cast<int>(logger_core::consumeRequestLineByte(overlong, 'B')));

  logger_core::RequestLineReader bareLf;
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::RequestLineReadResult::BAD_REQUEST),
      static_cast<int>(logger_core::consumeRequestLineByte(bareLf, '\n')));

  logger_core::RequestLineReader badCrlf;
  logger_core::consumeRequestLineByte(badCrlf, 'G');
  logger_core::consumeRequestLineByte(badCrlf, '\r');
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::RequestLineReadResult::BAD_REQUEST),
      static_cast<int>(logger_core::consumeRequestLineByte(badCrlf, 'X')));

  char exactTargetLine[logger_core::HTTP_MAX_REQUEST_LINE_BYTES + 1] = {};
  memcpy(exactTargetLine, "GET /", 5);
  memset(exactTargetLine + 5, 'a',
         logger_core::HTTP_MAX_REQUEST_TARGET_BYTES - 1);
  memcpy(exactTargetLine + 4 + logger_core::HTTP_MAX_REQUEST_TARGET_BYTES,
         " HTTP/1.1", 9);
  const size_t exactTargetLineLength =
      4 + logger_core::HTTP_MAX_REQUEST_TARGET_BYTES + 9;
  logger_core::HttpRequest request;
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HttpRequestParseResult::OK),
      static_cast<int>(logger_core::parseHttpRequestLine(
          exactTargetLine, exactTargetLineLength, request)));
  TEST_ASSERT_EQUAL_UINT32(logger_core::HTTP_MAX_REQUEST_TARGET_BYTES,
                           request.targetLength);

  char targetOverflow[logger_core::HTTP_MAX_REQUEST_LINE_BYTES + 1] = {};
  memcpy(targetOverflow, "GET /", 5);
  memset(targetOverflow + 5, 'a',
         logger_core::HTTP_MAX_REQUEST_TARGET_BYTES);
  memcpy(targetOverflow + 5 + logger_core::HTTP_MAX_REQUEST_TARGET_BYTES,
         " HTTP/1.1", 9);
  const size_t targetOverflowLength =
      5 + logger_core::HTTP_MAX_REQUEST_TARGET_BYTES + 9;
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HttpRequestParseResult::URI_TOO_LONG),
      static_cast<int>(logger_core::parseHttpRequestLine(
          targetOverflow, targetOverflowLength, request)));

  char lineOverflow[logger_core::HTTP_MAX_REQUEST_LINE_BYTES + 1] = {};
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HttpRequestParseResult::URI_TOO_LONG),
      static_cast<int>(logger_core::parseHttpRequestLine(
          lineOverflow, sizeof(lineOverflow), request)));
}

void testHttpHeaderByteStreamConfiguredLimits() {
  logger_core::HeaderParser exactLine;
  for (size_t i = 0; i < logger_core::HTTP_MAX_HEADER_LINE_BYTES; ++i) {
    TEST_ASSERT_EQUAL(
        static_cast<int>(logger_core::HeaderParseResult::IN_PROGRESS),
        static_cast<int>(
            logger_core::consumeHeaderByte(exactLine, 'A')));
  }
  logger_core::consumeHeaderByte(exactLine, '\r');
  logger_core::consumeHeaderByte(exactLine, '\n');
  logger_core::consumeHeaderByte(exactLine, '\r');
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::COMPLETE),
      static_cast<int>(logger_core::consumeHeaderByte(exactLine, '\n')));

  logger_core::HeaderParser longLine;
  for (size_t i = 0; i < logger_core::HTTP_MAX_HEADER_LINE_BYTES; ++i) {
    logger_core::consumeHeaderByte(longLine, 'A');
  }
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::TOO_LARGE),
      static_cast<int>(logger_core::consumeHeaderByte(longLine, 'B')));

  logger_core::HeaderParser exactCount;
  for (size_t i = 0; i < logger_core::HTTP_MAX_HEADER_COUNT; ++i) {
    logger_core::consumeHeaderByte(exactCount, 'X');
    logger_core::consumeHeaderByte(exactCount, '\r');
    logger_core::consumeHeaderByte(exactCount, '\n');
  }
  logger_core::consumeHeaderByte(exactCount, '\r');
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::COMPLETE),
      static_cast<int>(logger_core::consumeHeaderByte(exactCount, '\n')));

  logger_core::HeaderParser tooMany;
  for (size_t i = 0; i <= logger_core::HTTP_MAX_HEADER_COUNT; ++i) {
    logger_core::consumeHeaderByte(tooMany, 'X');
    logger_core::consumeHeaderByte(tooMany, '\r');
    const logger_core::HeaderParseResult result =
        logger_core::consumeHeaderByte(tooMany, '\n');
    if (i == logger_core::HTTP_MAX_HEADER_COUNT) {
      TEST_ASSERT_EQUAL(
          static_cast<int>(logger_core::HeaderParseResult::TOO_LARGE),
          static_cast<int>(result));
    }
  }

  logger_core::HeaderParser exactTotal;
  for (size_t line = 0; line < 7; ++line) {
    for (size_t i = 0; i < 254; ++i) {
      logger_core::consumeHeaderByte(exactTotal, 'A');
    }
    logger_core::consumeHeaderByte(exactTotal, '\r');
    logger_core::consumeHeaderByte(exactTotal, '\n');
  }
  for (size_t i = 0; i < 252; ++i) {
    logger_core::consumeHeaderByte(exactTotal, 'A');
  }
  logger_core::consumeHeaderByte(exactTotal, '\r');
  logger_core::consumeHeaderByte(exactTotal, '\n');
  logger_core::consumeHeaderByte(exactTotal, '\r');
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::COMPLETE),
      static_cast<int>(logger_core::consumeHeaderByte(exactTotal, '\n')));
  TEST_ASSERT_EQUAL_UINT32(logger_core::HTTP_MAX_HEADER_BYTES,
                           exactTotal.totalBytes);

  logger_core::HeaderParser totalOverflow;
  for (size_t line = 0; line < 8; ++line) {
    for (size_t i = 0; i < 254; ++i) {
      logger_core::consumeHeaderByte(totalOverflow, 'A');
    }
    logger_core::consumeHeaderByte(totalOverflow, '\r');
    logger_core::consumeHeaderByte(totalOverflow, '\n');
  }
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::TOO_LARGE),
      static_cast<int>(
          logger_core::consumeHeaderByte(totalOverflow, '\r')));

  logger_core::HeaderParser bareLf;
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::BAD_REQUEST),
      static_cast<int>(logger_core::consumeHeaderByte(bareLf, '\n')));
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::BAD_REQUEST),
      static_cast<int>(logger_core::finishHeaders(bareLf)));

  const char invalidControls[] = {'\0', '\x01', '\x1F', '\x7F'};
  for (char value : invalidControls) {
    logger_core::HeaderParser parser;
    TEST_ASSERT_EQUAL(
        static_cast<int>(logger_core::HeaderParseResult::BAD_REQUEST),
        static_cast<int>(logger_core::consumeHeaderByte(parser, value)));
  }

  logger_core::HeaderParser tabAllowed;
  TEST_ASSERT_EQUAL(
      static_cast<int>(logger_core::HeaderParseResult::IN_PROGRESS),
      static_cast<int>(logger_core::consumeHeaderByte(tabAllowed, '\t')));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(testRingBufferWrapFullReuseAndBounds);
  RUN_TEST(testBatteryEstimateTable);
  RUN_TEST(testSolarChargingTable);
  RUN_TEST(testPeriodicPollPolicyInitialNotDueDueAndRollover);
  RUN_TEST(testPowerSnapshotCopyKeepsManagerAndProbeValuesDistinct);
  RUN_TEST(testModbusCrcNullEmptyAndKnownFrame);
  RUN_TEST(testModbusValidationRejectsMalformedFrames);
  RUN_TEST(testModbusScannerNoCandidateAndFinalOffset);
  RUN_TEST(testModbusReadRequestGoldenVectorsAndBounds);
  RUN_TEST(testModbusRegisterDecodeHandlesEchoNoiseAndMalformedFrames);
  RUN_TEST(testExponentialBackoffTable);
  RUN_TEST(testDeadlineComparisonTableIncludingRollover);
  RUN_TEST(testNtpPolicyFirstAttemptAndSuccessInterval);
  RUN_TEST(testNtpPolicyFailureRetryForValidAndInvalidClocks);
  RUN_TEST(testNtpPolicyBoundedBackoffAndMillisRollover);
  RUN_TEST(testSensorDiscoveryPolicyTable);
  RUN_TEST(testHttpServerStartTransitionPolicy);
  RUN_TEST(testUploadStatusClassificationTable);
  RUN_TEST(testUploadRetryAndReadingActionsTable);
  RUN_TEST(testUploadCooldownRecordedOncePerLogicalOperation);
  RUN_TEST(testWeatherSummaryFirstSampleAndNumericAggregation);
  RUN_TEST(testWeatherSummaryRainfallDecreaseMarksReset);
  RUN_TEST(testSensorIdentityValidityTable);
  RUN_TEST(testUtcScheduleBootAtBoundaryAndRepeatedCalls);
  RUN_TEST(testUtcScheduleBootMidIntervalDoesNotFire);
  RUN_TEST(testUtcScheduleDelayedCrossingCatchesUpOnce);
  RUN_TEST(testUtcScheduleForwardJumpProducesSingleCurrentCatchUp);
  RUN_TEST(testUtcScheduleBackwardCorrectionAndRecovery);
  RUN_TEST(testUtcScheduleDayAndYearRollover);
  RUN_TEST(testUtcScheduleInvalidClockAndConfigurationDoNotMutateState);
  RUN_TEST(testUtcScheduleNonDivisorIntervalUsesEpochIdentity);
  RUN_TEST(testBoardProfilesTable);
  RUN_TEST(testSiteHealthClassification);
  RUN_TEST(testEpaperRefreshPolicyLifecycleAndRollover);
  RUN_TEST(testEpaperRefreshDecisionCommitsOnlyAfterSuccessfulRender);
  RUN_TEST(testSc16is752CommandsBaudDivisorsAndFraming);
  RUN_TEST(testEpaperPolicyRejectsInvalidConfigurationAndBoundsBusyWait);
  RUN_TEST(testRollingExtremaBucketsMergeExpireAndRejectInvalidValues);
  RUN_TEST(testHttpRequestRoutesAreExactAndQueriesAreNotAccepted);
  RUN_TEST(testHttpRequestRejectsMalformedLines);
  RUN_TEST(testHttpRequestLineCrlfAndConfiguredLimits);
  RUN_TEST(testHttpHeaderByteStreamConfiguredLimits);
  return UNITY_END();
}
