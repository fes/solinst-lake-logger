#pragma once

#include <stddef.h>
#include <stdint.h>

enum class LoggerRole : uint8_t {
  SOLINST_LOGGER,
  SITE_LOGGER
};

enum class DisplayBehavior : uint8_t {
  HEADLESS,
  WAKE_ON_DEMAND,
  PERSISTENT_EPAPER
};

struct BoardProfile {
  const char* name;
  LoggerRole role;
  uint8_t rs485ChannelCount;
  bool solinstEnabled;
  bool weatherEnabled;
  DisplayBehavior displayBehavior;
  size_t backlogCapacity;
};

constexpr BoardProfile OPTA_SOLINST_PROFILE = {
    "opta-solinst",
    LoggerRole::SOLINST_LOGGER,
    1,
    true,
    false,
    DisplayBehavior::WAKE_ON_DEMAND,
    32};

constexpr BoardProfile GIGA_SITE_PROFILE = {
    "giga-site",
    LoggerRole::SITE_LOGGER,
    2,
    true,
    true,
    DisplayBehavior::PERSISTENT_EPAPER,
    256};

#if defined(LOGGER_BOARD_OPTA) && defined(LOGGER_BOARD_GIGA)
#error "Select exactly one logger board profile"
#elif defined(LOGGER_BOARD_GIGA)
constexpr BoardProfile ACTIVE_BOARD_PROFILE = GIGA_SITE_PROFILE;
#else
constexpr BoardProfile ACTIVE_BOARD_PROFILE = OPTA_SOLINST_PROFILE;
#endif

static_assert(ACTIVE_BOARD_PROFILE.solinstEnabled,
              "Every current logger profile requires a Solinst channel");
