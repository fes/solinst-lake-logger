#include "logger_core/site_presentation.h"

namespace logger_core {

SiteHealth classifySiteHealth(const SiteSnapshot& snapshot) {
  if (!snapshot.clockValid || !snapshot.sensorFound) {
    return SiteHealth::CRITICAL;
  }
  if (!snapshot.waterValid || !snapshot.wifiConnected ||
      snapshot.backlogCount > 0 || snapshot.consecutiveUploadFailures > 0 ||
      !snapshot.batteryValid ||
      (snapshot.weatherEnabled && !snapshot.weatherValid)) {
    return SiteHealth::DEGRADED;
  }
  return SiteHealth::HEALTHY;
}

const char* siteHealthName(SiteHealth health) {
  switch (health) {
    case SiteHealth::HEALTHY: return "healthy";
    case SiteHealth::DEGRADED: return "degraded";
    case SiteHealth::CRITICAL: return "critical";
  }
  return "critical";
}

EpaperRefreshDecision decideEpaperRefresh(
    uint32_t nowMs, const SiteSnapshot& snapshot, uint32_t refreshIntervalMs,
    uint32_t fullRefreshIntervalMs, const EpaperRefreshState& state) {
  if (refreshIntervalMs == 0 || fullRefreshIntervalMs < refreshIntervalMs) {
    return EpaperRefreshDecision::NONE;
  }

  if (!state.initialized) {
    return EpaperRefreshDecision::FULL;
  }

  const bool fullRefreshDue =
      nowMs - state.lastFullRefreshMs >= fullRefreshIntervalMs;
  const bool regularRefreshDue =
      nowMs - state.lastRefreshMs >= refreshIntervalMs;
  const bool healthChanged = snapshot.health != state.lastHealth;
  const bool readingChanged =
      snapshot.readingRevision != state.lastReadingRevision;
  const bool weatherChanged =
      snapshot.weatherRevision != state.lastWeatherRevision;

  if (!fullRefreshDue && !regularRefreshDue && !healthChanged &&
      !readingChanged && !weatherChanged) {
    return EpaperRefreshDecision::NONE;
  }

  if (fullRefreshDue) {
    return EpaperRefreshDecision::FULL;
  }

  if (healthChanged || regularRefreshDue || readingChanged ||
      weatherChanged) {
    return EpaperRefreshDecision::PARTIAL;
  }
  return EpaperRefreshDecision::NONE;
}

void recordEpaperRefresh(
    uint32_t nowMs, const SiteSnapshot& snapshot,
    EpaperRefreshDecision decision, EpaperRefreshState& state) {
  if (decision == EpaperRefreshDecision::NONE) return;
  state.initialized = true;
  state.lastRefreshMs = nowMs;
  state.lastReadingRevision = snapshot.readingRevision;
  state.lastWeatherRevision = snapshot.weatherRevision;
  state.lastHealth = snapshot.health;
  if (decision == EpaperRefreshDecision::FULL) {
    state.lastFullRefreshMs = nowMs;
  }
}

EpaperRefreshDecision observeEpaperRefresh(
    uint32_t nowMs, const SiteSnapshot& snapshot, uint32_t refreshIntervalMs,
    uint32_t fullRefreshIntervalMs, EpaperRefreshState& state) {
  const EpaperRefreshDecision decision = decideEpaperRefresh(
      nowMs, snapshot, refreshIntervalMs, fullRefreshIntervalMs, state);
  recordEpaperRefresh(nowMs, snapshot, decision, state);
  return decision;
}

bool epaperBusyTimedOut(
    uint32_t nowMs, uint32_t busyStartedMs, uint32_t timeoutMs) {
  return timeoutMs == 0 || nowMs - busyStartedMs >= timeoutMs;
}

bool minuteInDailyWindow(
    uint16_t minuteOfDay, uint16_t startMinute, uint16_t endMinute) {
  constexpr uint16_t MINUTES_PER_DAY = 24U * 60U;
  if (minuteOfDay >= MINUTES_PER_DAY ||
      startMinute >= MINUTES_PER_DAY ||
      endMinute >= MINUTES_PER_DAY ||
      startMinute == endMinute) {
    return false;
  }
  if (startMinute < endMinute) {
    return minuteOfDay >= startMinute && minuteOfDay < endMinute;
  }
  return minuteOfDay >= startMinute || minuteOfDay < endMinute;
}

}  // namespace logger_core
