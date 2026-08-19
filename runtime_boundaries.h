#pragma once

// Narrow application-facing contracts. The Opta implementations delegate to
// the existing functions, so adopting these boundaries does not change field
// behavior. A future Giga target can provide different adapters while reusing
// scheduling, readings, backlog, HTTP, and upload logic.

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
  void (*pollIfDue)(bool force);
};

extern const PlatformBoundary ACTIVE_PLATFORM;
extern const DisplayBoundary ACTIVE_DISPLAY;
extern const AuxiliarySensorBoundary ACTIVE_AUXILIARY_SENSORS;

