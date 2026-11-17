#include "AudioWaterfallService.h"

#include <string.h>

bool AudioWaterfallService::begin(AudioCaptureService& capture) {
  if (active_) return true;
  if (!capture.begin()) return false;

  monitor_config_t cfg{};
  cfg.f_min = 100.0f;
  cfg.f_max = 1900.0f;
  cfg.sample_rate = AudioCaptureService::kSampleRate;
  cfg.time_osr = 1;
  cfg.freq_osr = 1;
  cfg.protocol = FTX_PROTOCOL_FT8;
  monitor_ = {};
  monitor_init(&monitor_, &cfg);

  if (!monitor_.window || !monitor_.last_frame || !monitor_.timedata ||
      !monitor_.freqdata || !monitor_.fft_work || !monitor_.fft_cfg ||
      !monitor_.wf.mag || monitor_.block_size != static_cast<int>(kFrameSamples)) {
    monitor_free(&monitor_);
    monitor_ = {};
    return false;
  }

  dcEstimate_ = 2048.0f;
  frameFill_ = 0;
  ringHead_ = ringTail_ = 0;
  droppedSamples_ = 0;
  rowWrite_ = rowCount_ = 0;
  memset(history_, 0, sizeof(history_));

  capture_ = &capture;
  if (!capture_->subscribe(this, &AudioWaterfallService::onSamples)) {
    monitor_free(&monitor_);
    monitor_ = {};
    capture_ = nullptr;
    return false;
  }

  monitorReady_ = true;
  active_ = true;
  return true;
}

void AudioWaterfallService::end() {
  if (capture_) capture_->unsubscribe(this);
  capture_ = nullptr;
  active_ = false;
  if (monitorReady_) monitor_free(&monitor_);
  monitor_ = {};
  monitorReady_ = false;
  frameFill_ = 0;
  rowWrite_ = rowCount_ = 0;
  ringHead_ = ringTail_ = 0;
}

void AudioWaterfallService::onSamples(void* context, const int16_t* samples,
                                      size_t count, int64_t) {
  auto* self = static_cast<AudioWaterfallService*>(context);
  if (!self || !self->active_ || !samples) return;

  portENTER_CRITICAL(&self->ringMux_);
  for (size_t i = 0; i < count; ++i) {
    const size_t next = (self->ringHead_ + 1) % kRingSamples;
    if (next == self->ringTail_) {
      self->ringTail_ = (self->ringTail_ + 1) % kRingSamples;
      ++self->droppedSamples_;
    }
    self->ring_[self->ringHead_] = samples[i];
    self->ringHead_ = next;
  }
  portEXIT_CRITICAL(&self->ringMux_);
}

void AudioWaterfallService::poll() {
  if (!active_ || !monitorReady_) return;

  // DSP runs in the main loop, never in the time-critical ADC sampling task.
  for (;;) {
    int16_t sample = 0;
    bool haveSample = false;
    portENTER_CRITICAL(&ringMux_);
    if (ringTail_ != ringHead_) {
      sample = ring_[ringTail_];
      ringTail_ = (ringTail_ + 1) % kRingSamples;
      haveSample = true;
    }
    portEXIT_CRITICAL(&ringMux_);
    if (!haveSample) break;

    dcEstimate_ += (static_cast<float>(sample) - dcEstimate_) * 0.001f;
    float audio = (static_cast<float>(sample) - dcEstimate_) / 2048.0f;
    if (audio > 1.0f) audio = 1.0f;
    if (audio < -1.0f) audio = -1.0f;
    frame_[frameFill_++] = audio;

    if (frameFill_ >= kFrameSamples) {
      monitor_process(&monitor_, frame_);
      frameFill_ = 0;
      pushRow();
      // monitor_t's rolling allocation is one FT8 slot long (50 rows).
      // Copy every row into our own circular history before resetting it.
      if (monitor_.wf.num_blocks >= monitor_.wf.max_blocks) monitor_reset(&monitor_);
    }
  }
}

void AudioWaterfallService::pushRow() {
  if (!monitor_.wf.mag || monitor_.wf.num_blocks <= 0 || monitor_.wf.num_bins <= 0) return;
  const int sourceRow = monitor_.wf.num_blocks - 1;
  const int sourceBase = sourceRow * monitor_.wf.block_stride;
  for (uint16_t x = 0; x < kBins; ++x) {
    int sourceBin = (static_cast<int>(x) * monitor_.wf.num_bins) / kBins;
    if (sourceBin >= monitor_.wf.num_bins) sourceBin = monitor_.wf.num_bins - 1;
    const int sourceIndex = sourceBase + sourceBin * monitor_.wf.freq_osr;
    history_[rowWrite_][x] = static_cast<uint8_t>(monitor_.wf.mag[sourceIndex]);
  }
  rowWrite_ = static_cast<uint16_t>((rowWrite_ + 1) % kRows);
  if (rowCount_ < kRows) ++rowCount_;
}

bool AudioWaterfallService::copyWaterfall(uint8_t* out, uint16_t maxRows,
                                          uint16_t outBins, uint16_t& rows,
                                          uint16_t& bins) const {
  rows = bins = 0;
  if (!out || !maxRows || !outBins || !rowCount_) return false;
  rows = rowCount_ < maxRows ? rowCount_ : maxRows;
  bins = outBins;
  const uint16_t first = static_cast<uint16_t>((rowWrite_ + kRows - rows) % kRows);
  for (uint16_t y = 0; y < rows; ++y) {
    const uint16_t sourceRow = static_cast<uint16_t>((first + y) % kRows);
    for (uint16_t x = 0; x < outBins; ++x) {
      const uint16_t sourceBin = static_cast<uint16_t>(
        (static_cast<uint32_t>(x) * kBins) / outBins);
      out[static_cast<size_t>(y) * outBins + x] = history_[sourceRow][sourceBin];
    }
  }
  return true;
}
