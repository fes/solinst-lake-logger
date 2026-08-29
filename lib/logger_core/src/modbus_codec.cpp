#include "logger_core/modbus_codec.h"

#include "logger_core/domain_logic.h"

namespace logger_core {
namespace {

bool validReadArguments(uint8_t slaveId, uint8_t functionCode,
                        uint16_t quantity) {
  return slaveId >= 1 && slaveId <= 247 &&
         (functionCode == 0x03 || functionCode == 0x04) && quantity >= 1 &&
         quantity <= LOGGER_MODBUS_MAX_READ_REGISTERS;
}

}  // namespace

size_t buildModbusReadRequest(uint8_t slaveId, uint8_t functionCode,
                              uint16_t startRegister, uint16_t quantity,
                              uint8_t* output, size_t outputCapacity) {
  if (!validReadArguments(slaveId, functionCode, quantity) ||
      output == nullptr || outputCapacity < MODBUS_READ_REQUEST_BYTES) {
    return 0;
  }

  output[0] = slaveId;
  output[1] = functionCode;
  output[2] = static_cast<uint8_t>(startRegister >> 8U);
  output[3] = static_cast<uint8_t>(startRegister);
  output[4] = static_cast<uint8_t>(quantity >> 8U);
  output[5] = static_cast<uint8_t>(quantity);
  const uint16_t crc = modbusCrc16(output, 6);
  output[6] = static_cast<uint8_t>(crc);
  output[7] = static_cast<uint8_t>(crc >> 8U);
  return MODBUS_READ_REQUEST_BYTES;
}

ModbusDecodeResult decodeModbusReadRegisters(
    const uint8_t* buffer, size_t length, uint8_t slaveId,
    uint8_t functionCode, uint16_t quantity, uint16_t* values,
    size_t valueCapacity, size_t* frameOffset) {
  if (frameOffset != nullptr) *frameOffset = 0;
  if (!validReadArguments(slaveId, functionCode, quantity) ||
      buffer == nullptr || values == nullptr || valueCapacity < quantity) {
    return ModbusDecodeResult::INVALID_ARGUMENT;
  }

  const uint8_t byteCount = static_cast<uint8_t>(quantity * 2U);
  const int offset =
      findValidModbusResponse(buffer, length, slaveId, functionCode, byteCount);
  if (offset < 0) return ModbusDecodeResult::FRAME_NOT_FOUND;

  const uint8_t* response = buffer + offset;
  for (uint16_t i = 0; i < quantity; ++i) {
    const size_t valueOffset = 3U + static_cast<size_t>(i) * 2U;
    values[i] =
        (static_cast<uint16_t>(response[valueOffset]) << 8U) |
        static_cast<uint16_t>(response[valueOffset + 1U]);
  }
  if (frameOffset != nullptr) *frameOffset = static_cast<size_t>(offset);
  return ModbusDecodeResult::OK;
}

}  // namespace logger_core
