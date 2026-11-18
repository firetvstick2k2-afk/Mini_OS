#pragma once
#include <Arduino.h>
#include "FmPanoramaService.h"

// Repeated RF sweeps with a small chronological history ring.
// Kept separate from the one-shot FM panorama page and from audio FFT.
class FmLivePanoramaService {
public:
  using Point = FmPanoramaService::Point;
  static constexpr uint8_t kHistoryRows = 28;
  bool start(RadioCore& radio, uint32_t centerKHz);
  void tick(RadioCore& radio);
  void stop(RadioCore& radio);
  bool active() const { return active_; }
  uint8_t rowCount() const { return rowCount_; }
  uint32_t centerFrequencyKHz() const { return centerKHz_; }
  const Point* row(uint8_t chronologicalIndex) const;
private:
  FmPanoramaService sweep_;
  Point history_[kHistoryRows][FmPanoramaService::kPointCount]{};
  uint8_t nextRow_ = 0;
  uint8_t rowCount_ = 0;
  uint32_t centerKHz_ = 0;
  uint32_t restartAtMs_ = 0;
  bool active_ = false;
  bool restartPending_ = false;
};
