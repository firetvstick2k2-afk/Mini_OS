#pragma once
#include <Arduino.h>
#include "ScanService.h"
#include "RadioCore.h"
class FmPanoramaService {
public:
  using Point = ScanService::Point;
  static constexpr uint16_t kPointCount = 21;
  enum class State : uint8_t { Idle, Running, Done, Aborted, Error };
  bool start(RadioCore& radio, uint32_t centerKHz);
  void tick(RadioCore& radio);
  void stop(RadioCore& radio);
  void invalidate();
  State state() const { return state_; }
  uint16_t count() const { return count_; }
  uint16_t capacity() const { return kPointCount; }
  const Point* data() const { return points_; }
private:
  Point points_[kPointCount]{};
  State state_=State::Idle;
  uint16_t count_=0;
  uint32_t startKHz_=0, savedFrequencyKHz_=0, lastTuneMs_=0;
  bool waitingForSample_=false;
  void restore(RadioCore& radio);
};
