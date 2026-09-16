#include "config.h"

namespace {

void copyDiagnosticText(char* destination, size_t destinationSize,
                        const char* source) {
  if (destination == nullptr || destinationSize == 0) return;
  snprintf(destination, destinationSize, "%s",
           source == nullptr ? "unknown" : source);
}

// Attempts an automatic hardware recovery once a single channel has
// racked up MODBUS_BRIDGE_RECOVERY_THRESHOLD consecutive failures. This is
// deliberately gated on a *consecutive* count (reset on any success) so a
// single transient miss doesn't churn the bridge, but a real stuck/faulted
// condition gets an automatic attempt at clearing itself well before a
// human ever looks at it.
void maybeAttemptBridgeRecovery(const char* channelName, Rs485Channel& channel,
                                uint32_t& consecutiveFailures) {
  consecutiveFailures++;
  if (consecutiveFailures < MODBUS_BRIDGE_RECOVERY_THRESHOLD) return;

  Serial.print(channelName);
  Serial.print(" channel: ");
  Serial.print(consecutiveFailures);
  Serial.println(" consecutive Modbus failures, attempting RS-485 bridge recovery");

  rs485BridgeRecoveryAttempts++;
  if (channel.attemptRecovery()) {
    rs485BridgeRecoverySuccesses++;
    Serial.println("RS-485 bridge recovery self-test passed");
  } else {
    Serial.println("RS-485 bridge recovery not supported on this board, or self-test failed");
  }
  consecutiveFailures = 0;
}

}  // namespace

void recordModbusFailure(const char* channelName, Rs485Channel& channel,
                         uint8_t slaveId, uint8_t functionCode,
                         uint16_t startRegister, uint16_t quantity,
                         const char* reason, size_t responseLength) {
  const size_t index = logger_core::reserveDiagnosticHistoryEntry(
      modbusFailureHistoryState, MODBUS_FAILURE_HISTORY_CAPACITY);
  if (index != SIZE_MAX) {
    ModbusFailureDiagnostic& diagnostic = modbusFailureHistory[index];
    const String timestamp = nowUtcString();
    copyDiagnosticText(diagnostic.timestampUtc,
                       sizeof(diagnostic.timestampUtc), timestamp.c_str());
    copyDiagnosticText(diagnostic.channel, sizeof(diagnostic.channel), channelName);
    copyDiagnosticText(diagnostic.reason, sizeof(diagnostic.reason), reason);
    diagnostic.slaveId = slaveId;
    diagnostic.functionCode = functionCode;
    diagnostic.startRegister = startRegister;
    diagnostic.quantity = quantity;
    diagnostic.responseLength =
        responseLength > UINT16_MAX ? UINT16_MAX
                                    : static_cast<uint16_t>(responseLength);

    // Snapshot power state without forcing a fresh INA228 read (that would
    // add I2C latency to every failed Modbus attempt); the periodic poll
    // interval is short enough that this is still a close-in-time reading.
    pollPowerMonitorsIfDue();
    diagnostic.batteryVoltageValid = latestPowerSnapshot.batteryOutput.valid;
    diagnostic.batteryOutputVoltageV =
        latestPowerSnapshot.batteryOutput.valid
            ? latestPowerSnapshot.batteryOutput.busVoltageV
            : NAN;
    diagnostic.solarVoltageValid = latestPowerSnapshot.solarInput.valid;
    diagnostic.solarInputVoltageV =
        latestPowerSnapshot.solarInput.valid
            ? latestPowerSnapshot.solarInput.busVoltageV
            : NAN;
    diagnostic.batteryChargePct = logger_core::batteryChargePercent(
        latestPowerSnapshot.batteryOutput.valid,
        latestPowerSnapshot.batteryOutput.busVoltageV);
    diagnostic.solarChargingNow = logger_core::solarCharging(
        latestPowerSnapshot.solarInput.valid,
        latestPowerSnapshot.solarInput.currentA);

    diagnostic.bridgeHealth = channel.health();
  }

  if (strcmp(channelName, "solinst") == 0) {
    maybeAttemptBridgeRecovery(channelName, channel, consecutiveSolinstModbusFailures);
  } else if (strcmp(channelName, "weather") == 0) {
    maybeAttemptBridgeRecovery(channelName, channel, consecutiveWeatherModbusFailures);
  }
}
