#pragma once

// Copy this file to hil_config.h and replace every pin/channel value from the
// reviewed wiring diagram. The placeholder CI build leaves all hardware off.
#define HIL_DEVICE_NAME "giga-hil-unconfigured"

#define HIL_RS485_CHANNEL1_ENABLED 0
#define HIL_RS485_CHANNEL2_ENABLED 0
#define HIL_RS485_CS_PIN 5
#define HIL_RS485_IRQ_PIN 4
#define HIL_RS485_CHANNEL1_ENABLE_PIN 3
#define HIL_RS485_CHANNEL2_ENABLE_PIN 2
#define HIL_RS485_TRANSMIT_ENABLE_LEVEL LOW
