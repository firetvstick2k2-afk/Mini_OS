#pragma once

#include <Arduino.h>
#include "AudioCaptureService.h"
#include "../digital/ftx/ft8_lib/common/monitor.h"

// Continuous audio-spectrum waterfall. It reuses the same GPIO11 capture
// stream and KISS FFT monitor as FT8, but has its own rolling history and
// never changes or resets the FT8 decoder's waterfall.
class AudioWaterfallService {
public:
  static constexpr uint16_t kRows = 64;
  static constexpr uint16_t kBins = 300;

  bool begin(AudioCaptureService& capture);
  void end();
  void poll();
  bool active() const { return active_; }
  bool available() const { return rowCount_ != 0; }
  uint32_t droppedSamples() const { return droppedSamples_; }
  bool copyWaterfall(uint8_t* out, uint16_t maxRows, uint16_t outBins,
                     uint16_t& rows, uint16_t& bins) const;

private:
  static constexpr size_t kFrameSamples = 640;
  static constexpr size_t kRingSamples = 2048;
  static void onSamples(void* context, const int16_t* samples,
                        size_t count, int64_t utcStartMicros);
  void pushRow();

  AudioCaptureService* capture_ = nullptr;
  monitor_t monitor_{};
  bool monitorReady_ = false;
  bool active_ = false;
  float dcEstimate_ = 2048.0f;
  float frame_[kFrameSamples]{};
  size_t frameFill_ = 0;

  int16_t ring_[kRingSamples]{};
  volatile size_t ringHead_ = 0;
  volatile size_t ringTail_ = 0;
  portMUX_TYPE ringMux_ = portMUX_INITIALIZER_UNLOCKED;
  uint32_t droppedSamples_ = 0;

  uint8_t history_[kRows][kBins]{};
  uint16_t rowWrite_ = 0;
  uint16_t rowCount_ = 0;
};
