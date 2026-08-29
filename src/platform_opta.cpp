#include "config.h"
#include "runtime_boundaries.h"

#if defined(LOGGER_BOARD_OPTA)
namespace {

void beginOptaPlatform() {
  initUserInterface();
}

void handleOptaInput() {
  handleUserButton();
}

void tickOptaPlatform() {
  updateUserInterface();
}

bool beginOptaDisplay() {
  return initDisplay();
}

void tickOptaDisplay() {
  updateDisplay();
}

void beginOptaAuxiliarySensors() {
  initPowerMonitors();
  initWeatherSensor();
}

void pollOptaAuxiliarySensors(bool force) {
  pollWeatherIfDue(force);
}

}  // namespace

const PlatformBoundary ACTIVE_PLATFORM = {
    beginOptaPlatform,
    handleOptaInput,
    tickOptaPlatform};

const DisplayBoundary ACTIVE_DISPLAY = {
    beginOptaDisplay,
    tickOptaDisplay};

const AuxiliarySensorBoundary ACTIVE_AUXILIARY_SENSORS = {
    beginOptaAuxiliarySensors,
    pollPowerMonitorsIfDue,
    pollOptaAuxiliarySensors};
#endif
