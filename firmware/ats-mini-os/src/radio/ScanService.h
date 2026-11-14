#pragma once
#include <Arduino.h>
#include "RadioCore.h"

// Cooperative 256-point sweep; tick() performs at most one small operation.
class ScanService {
public:
  static constexpr uint16_t kMaxPoints = 256;
  struct Point { uint32_t frequencyKHz=0; uint8_t rssi=0; uint8_t snr=0; };
  enum class State : uint8_t { Idle, Running, Done, Aborted, Error };
  bool start(RadioCore& radio);
  void tick(RadioCore& radio);
  void stop(RadioCore& radio);
  State state() const { return state_; }
  uint16_t count() const { return count_; }
  uint16_t capacity() const { return planned_; }
  const Point& point(uint16_t index) const;
  uint32_t savedFrequencyKHz() const { return savedFrequencyKHz_; }
private:
  Point points_[kMaxPoints]{};
  State state_=State::Idle;
  uint16_t count_=0, planned_=0;
  uint32_t startKHz_=0, stepKHz_=0, savedFrequencyKHz_=0;
  uint32_t lastTuneMs_=0;
  bool waitingForSample_=false;
  void restore(RadioCore& radio);
};
