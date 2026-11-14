#pragma once
#include <Arduino.h>
#include "RadioCore.h"

// Cooperative seek using the active band's step and the live RSSI snapshot.
class SeekService {
public:
  enum class State : uint8_t { Idle, Running, Found, NotFound, Aborted, Error };
  bool start(RadioCore& radio, int8_t direction=1, uint8_t threshold=12);
  void tick(RadioCore& radio);
  void stop() { if(state_==State::Running) state_=State::Aborted; }
  State state() const { return state_; }
private:
  State state_=State::Idle;
  uint32_t originKHz_=0, lastCandidateKHz_=0;
  uint32_t minimumKHz_=0, maximumKHz_=0, stepKHz_=0, lastTuneMs_=0;
  int8_t direction_=1;
  uint8_t threshold_=12;
  uint16_t attempts_=0;
};
