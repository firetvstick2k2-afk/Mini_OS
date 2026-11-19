#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include "AudioCaptureService.h"

// Continuous, independent audio FFT for the FM live waterfall.
// It consumes the shared audio stream but never touches the FT8/FT4 decoder.
class FmAudioFftService {
public:
  static constexpr uint8_t kBins = 64;
  static constexpr uint8_t kRows = 64;
  static constexpr size_t kFftSize = 512;
  static constexpr uint32_t kSamplesPerRow = 333; // About 12 rows/s at 4 kHz.

  bool enter(AudioCaptureService& capture);
  void exit();
  void update(int16_t rssi, uint32_t frequencyKHz);
  bool active() const { return active_; }
  bool available() const { portENTER_CRITICAL(&mux_); const bool ready = rowsFilled_ != 0; portEXIT_CRITICAL(&mux_); return ready; }
  void copyWaterfall(uint8_t* destination) const;

private:
  static void onAudioSamples(void* context, const int16_t* samples, size_t count, int64_t utcStartMicros);
  void pushSamples(const int16_t* samples, size_t count);
  void processFrame(int16_t rssi);

  AudioCaptureService* capture_ = nullptr;
  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  int16_t sampleRing_[kFftSize]{};
  uint32_t samplesSeen_ = 0;
  uint32_t lastFrameAt_ = 0;
  uint32_t lastFrequencyKHz_ = 0;
  uint32_t holdRowsUntilMs_ = 0;
  float smoothedBins_[kBins]{};
  bool haveSmoothedBins_ = false;
  uint8_t waterfall_[kRows][kBins]{};
  uint8_t nextRow_ = 0;
  uint8_t rowsFilled_ = 0;
  volatile bool active_ = false;
  bool attempted_ = false;
  uint32_t framesProcessed_ = 0;
};
