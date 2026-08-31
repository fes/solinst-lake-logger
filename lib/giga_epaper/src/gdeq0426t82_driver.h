#pragma once

#include <gdeq/GxEPD2_426_GDEQ0426T82.h>

class Gdeq0426t82Driver : public GxEPD2_426_GDEQ0426T82 {
 public:
  using GxEPD2_426_GDEQ0426T82::GxEPD2_426_GDEQ0426T82;
  using GxEPD2_426_GDEQ0426T82::init;
  using GxEPD2_426_GDEQ0426T82::refresh;

  void init(uint32_t serialDiagBitrate, bool initial,
            uint16_t resetDuration = 10, bool pulldownRstMode = false) {
    GxEPD2_426_GDEQ0426T82::init(
        serialDiagBitrate, initial, resetDuration, pulldownRstMode);
    if (initial) {
      // Prime both RAM planes without invoking the base clear, whose
      // non-virtual refresh would run GxEPD2's defective 90 C waveform.
      _initial_write = false;
      writeScreenBufferAgain();
    }
  }

  void refresh(bool partialUpdateMode = false) {
    if (partialUpdateMode) {
      GxEPD2_426_GDEQ0426T82::refresh(true);
      return;
    }

    _writeCommand(0x21);
    _writeData(0x40);
    _writeData(0x00);
    _writeCommand(0x1a);
    _writeData(0x19);  // 25 C selects the room-temperature OTP waveform.
    _writeCommand(0x22);
    _writeData(0x91);
    _writeCommand(0x20);
    _waitWhileBusy("Gdeq0426t82Driver LUT", 5000);
    _writeCommand(0x22);
    _writeData(0xc7);
    _writeCommand(0x20);
    _waitWhileBusy("Gdeq0426t82Driver display", 5000);
    _initial_refresh = false;
    _power_is_on = false;
  }
};
