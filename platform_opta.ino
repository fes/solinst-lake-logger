// Opta adapters for the application-facing runtime boundaries.
//
// These wrappers intentionally preserve the existing implementations. They are
// a seam for the future Giga site logger, not a rewrite of proven Opta code.

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

} // namespace

const PlatformBoundary ACTIVE_PLATFORM = {
  beginOptaPlatform,
  handleOptaInput,
  tickOptaPlatform
};

const DisplayBoundary ACTIVE_DISPLAY = {
  beginOptaDisplay,
  tickOptaDisplay
};

const AuxiliarySensorBoundary ACTIVE_AUXILIARY_SENSORS = {
  beginOptaAuxiliarySensors,
  pollOptaAuxiliarySensors
};

