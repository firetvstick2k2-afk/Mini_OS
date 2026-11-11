#pragma once
#include <Arduino.h>
#include "../radio/RadioCore.h"

// ATS Mini DEV-compatible frequency keypad state and input handling.
class FrequencyKeypad {
public:
  struct View {
    char input[15];
    uint8_t mode;       // FM, LSB, USB, AM (ATS Mini DEV order)
    uint8_t index;      // 0..16; 14 is skipped
    bool inputInMHz;
    bool rangeError;
  };

  void open(const RadioSnapshot& radioState);
  void rotate(int16_t delta);
  // Returns true while the keypad remains open; false means CLOSE/OK finished.
  bool press(RadioCore& radio);
  const View& view() const { return view_; }
  bool active() const { return active_; }

private:
  View view_{};
  uint8_t inputLength_ = 0;
  bool active_ = false;
  void clearInput();
};
