#pragma once

// Waveshare 2-CH RS485 HAT: SC16IS752 dual UART on the shared SPI1 bus.
constexpr int GIGA_RS485_CS_PIN = 5;
constexpr int GIGA_RS485_IRQ_PIN = 4;
constexpr int GIGA_RS485_CHANNEL1_ENABLE_PIN = 3;
constexpr int GIGA_RS485_CHANNEL2_ENABLE_PIN = 2;
constexpr uint8_t GIGA_RS485_TRANSMIT_ENABLE_LEVEL = LOW;

// Waveshare SKU 26376: GDEQ0426T82 / SSD1677, 800x480 black and white.
// SPI1 uses D11 MOSI and D13 SCK on the standard Giga header.
constexpr bool GIGA_EPAPER_ENABLED = true;
constexpr int GIGA_EPAPER_CS_PIN = 10;
constexpr int GIGA_EPAPER_DC_PIN = 9;
constexpr int GIGA_EPAPER_RST_PIN = 8;
constexpr int GIGA_EPAPER_BUSY_PIN = 7;
constexpr int GIGA_EPAPER_POWER_PIN = 6;
constexpr uint8_t GIGA_EPAPER_POWER_ENABLE_LEVEL = HIGH;
constexpr uint8_t GIGA_EPAPER_BUSY_LEVEL = HIGH;
constexpr uint32_t GIGA_EPAPER_REFRESH_INTERVAL_MS = 15UL * 60UL * 1000UL;
constexpr uint32_t GIGA_EPAPER_FULL_REFRESH_INTERVAL_MS =
    24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t GIGA_EPAPER_BUSY_TIMEOUT_MS = 10UL * 1000UL;
