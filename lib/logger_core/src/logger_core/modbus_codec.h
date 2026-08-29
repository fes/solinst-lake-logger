#pragma once

#include <stddef.h>
#include <stdint.h>

namespace logger_core {

constexpr uint16_t LOGGER_MODBUS_MAX_READ_REGISTERS = 125;
constexpr size_t MODBUS_READ_REQUEST_BYTES = 8;

enum class ModbusDecodeResult : uint8_t {
  OK,
  INVALID_ARGUMENT,
  FRAME_NOT_FOUND
};

size_t buildModbusReadRequest(uint8_t slaveId, uint8_t functionCode,
                              uint16_t startRegister, uint16_t quantity,
                              uint8_t* output, size_t outputCapacity);

ModbusDecodeResult decodeModbusReadRegisters(
    const uint8_t* buffer, size_t length, uint8_t slaveId,
    uint8_t functionCode, uint16_t quantity, uint16_t* values,
    size_t valueCapacity, size_t* frameOffset = nullptr);

}  // namespace logger_core
