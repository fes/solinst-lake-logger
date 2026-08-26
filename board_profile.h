#pragma once

#include <stdint.h>

// Build profiles describe hardware capabilities; they do not contain secrets or
// per-site calibration. The existing Opta logger remains the only selectable
// profile until the Giga hardware adapters are implemented and tested.

enum class LoggerRole : uint8_t {
  SOLINST_LOGGER,
  SITE_LOGGER
};

enum class DisplayBehavior : uint8_t {
  WAKE_ON_DEMAND,
  PERSISTENT_EPAPER
};

struct BoardProfile {
  const char *name;
  LoggerRole role;
  uint8_t rs485ChannelCount;
  bool solinstEnabled;
  bool weatherEnabled;
  DisplayBehavior displayBehavior;
};

constexpr BoardProfile OPTA_SOLINST_PROFILE = {
  "opta-solinst",
  LoggerRole::SOLINST_LOGGER,
  1,
  true,
  false,
  DisplayBehavior::WAKE_ON_DEMAND
};

// Planning target only. Keeping this declaration here makes the intended
// capability split explicit without pretending the Giga pin map, RS-485 HAT,
// or e-paper driver is ready to compile.
constexpr BoardProfile GIGA_SITE_PROFILE = {
  "giga-site",
  LoggerRole::SITE_LOGGER,
  2,
  true,
  true,
  DisplayBehavior::PERSISTENT_EPAPER
};

// The current sketch deliberately defaults to the known-working Opta profile.
// A future Giga sketch/profile selector will choose GIGA_SITE_PROFILE after its
// platform adapters exist.
constexpr BoardProfile ACTIVE_BOARD_PROFILE = OPTA_SOLINST_PROFILE;

static_assert(ACTIVE_BOARD_PROFILE.role == LoggerRole::SOLINST_LOGGER,
              "Only the Opta Solinst logger profile is implemented today");
static_assert(!ACTIVE_BOARD_PROFILE.weatherEnabled,
              "The Opta profile is intentionally Solinst-only");
