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

  if (!fullRefreshDue && !regularRefreshDue && !healthChanged &&
      !readingChanged) {
    return EpaperRefreshDecision::NONE;
  }

  if (fullRefreshDue) {
    return EpaperRefreshDecision::FULL;
  }

  if (healthChanged || regularRefreshDue || readingChanged) {
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

}  // namespace logger_core
