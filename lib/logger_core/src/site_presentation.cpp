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

}  // namespace logger_core
