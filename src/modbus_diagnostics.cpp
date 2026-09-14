#include "config.h"

namespace {

void copyDiagnosticText(char* destination, size_t destinationSize,
                        const char* source) {
  if (destination == nullptr || destinationSize == 0) return;
  snprintf(destination, destinationSize, "%s",
           source == nullptr ? "unknown" : source);
}

}  // namespace

void recordModbusFailure(const char* channel, uint8_t slaveId,
                         uint8_t functionCode, uint16_t startRegister,
                         uint16_t quantity, const char* reason,
                         size_t responseLength) {
  const size_t index = logger_core::reserveDiagnosticHistoryEntry(
      modbusFailureHistoryState, MODBUS_FAILURE_HISTORY_CAPACITY);
  if (index == SIZE_MAX) return;

  ModbusFailureDiagnostic& diagnostic = modbusFailureHistory[index];
  const String timestamp = nowUtcString();
  copyDiagnosticText(diagnostic.timestampUtc,
                     sizeof(diagnostic.timestampUtc), timestamp.c_str());
  copyDiagnosticText(diagnostic.channel, sizeof(diagnostic.channel), channel);
  copyDiagnosticText(diagnostic.reason, sizeof(diagnostic.reason), reason);
  diagnostic.slaveId = slaveId;
  diagnostic.functionCode = functionCode;
  diagnostic.startRegister = startRegister;
  diagnostic.quantity = quantity;
  diagnostic.responseLength =
      responseLength > UINT16_MAX ? UINT16_MAX
                                  : static_cast<uint16_t>(responseLength);
}
