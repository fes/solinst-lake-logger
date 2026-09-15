#pragma once

// Waveshare 2-CH RS485 HAT: SC16IS752 dual UART on the shared SPI1 bus.
constexpr int GIGA_RS485_CS_PIN = 5;
constexpr int GIGA_RS485_IRQ_PIN = 4;
constexpr int GIGA_RS485_CHANNEL1_ENABLE_PIN = 3;
constexpr int GIGA_RS485_CHANNEL2_ENABLE_PIN = 2;
constexpr uint8_t GIGA_RS485_TRANSMIT_ENABLE_LEVEL = LOW;

// Inkplate 6MOTION wired display on Giga Serial1: D1 TX, D0 RX.
constexpr uint32_t GIGA_INKPLATE_BAUD = 115200;
constexpr uint32_t GIGA_INKPLATE_DETECT_TIMEOUT_MS = 1500;
constexpr uint32_t GIGA_INKPLATE_ACK_TIMEOUT_MS = 10000;
constexpr uint32_t GIGA_INKPLATE_REDETECT_INTERVAL_MS = 60UL * 1000UL;
constexpr uint32_t GIGA_INKPLATE_REFRESH_INTERVAL_MS =
    15UL * 60UL * 1000UL;
