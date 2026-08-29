#pragma once

struct PlatformBoundary {
  void (*begin)();
  void (*handleInput)();
  void (*tick)();
};

struct DisplayBoundary {
  bool (*begin)();
  void (*tick)();
};

struct AuxiliarySensorBoundary {
  void (*begin)();
  void (*pollPowerIfDue)(bool force);
  void (*pollIfDue)(bool force);
};

extern const PlatformBoundary ACTIVE_PLATFORM;
extern const DisplayBoundary ACTIVE_DISPLAY;
extern const AuxiliarySensorBoundary ACTIVE_AUXILIARY_SENSORS;
