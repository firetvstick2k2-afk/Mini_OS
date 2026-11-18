#include "FmLivePanoramaService.h"
#include <string.h>

bool FmLivePanoramaService::start(RadioCore& radio, uint32_t centerKHz) {
  if (active_ || !radio.isReady() ||
      radio.snapshot().modulation != RadioModulation::FM ||
      centerKHz < 88500 || centerKHz > 107000) return false;
  memset(history_, 0, sizeof(history_));
  nextRow_ = 0;
  rowCount_ = 0;
  centerKHz_ = centerKHz;
  restartAtMs_ = 0;
  restartPending_ = false;
  active_ = sweep_.start(radio, centerKHz_);
  if (active_) Serial.printf("[FM LIVE PANORAMA] started at %.2f MHz\n", centerKHz_ / 1000.0f);
  return active_;
}

void FmLivePanoramaService::tick(RadioCore& radio) {
  if (!active_) return;
  if (sweep_.state() == FmPanoramaService::State::Running) {
    sweep_.tick(radio);
    return;
  }
  if (sweep_.state() == FmPanoramaService::State::Done) {
    if (!restartPending_) {
      const Point* completed = sweep_.data();
      if (completed && sweep_.count() == FmPanoramaService::kPointCount) {
        memcpy(history_[nextRow_], completed, sizeof(history_[nextRow_]));
        nextRow_ = static_cast<uint8_t>((nextRow_ + 1) % kHistoryRows);
        if (rowCount_ < kHistoryRows) ++rowCount_;
      }
      restartAtMs_ = millis() + 180;
      restartPending_ = true;
    }
    if (static_cast<int32_t>(millis() - restartAtMs_) >= 0) {
      restartAtMs_ = 0;
      restartPending_ = false;
      if (!sweep_.start(radio, centerKHz_)) {
        active_ = false;
        Serial.println("[FM LIVE PANORAMA] next sweep could not start");
      }
    }
    return;
  }
  if (sweep_.state() == FmPanoramaService::State::Error ||
      sweep_.state() == FmPanoramaService::State::Aborted) {
    active_ = false;
  }
}

void FmLivePanoramaService::stop(RadioCore& radio) {
  if (!active_) return;
  sweep_.stop(radio);
  active_ = false;
  restartAtMs_ = 0;
  restartPending_ = false;
  Serial.println("[FM LIVE PANORAMA] stopped");
}

const FmLivePanoramaService::Point* FmLivePanoramaService::row(uint8_t chronologicalIndex) const {
  if (chronologicalIndex >= rowCount_) return nullptr;
  const uint8_t oldest = static_cast<uint8_t>((nextRow_ + kHistoryRows - rowCount_) % kHistoryRows);
  const uint8_t physical = static_cast<uint8_t>((oldest + chronologicalIndex) % kHistoryRows);
  return history_[physical];
}
