#include "FmAudioFftService.h"
#include <math.h>
#include <string.h>

bool FmAudioFftService::enter(AudioCaptureService& capture) {
  if (active_) return capture_ == &capture;
#if defined(LILYGO_SI473X)
  (void)capture;
  return false;
#else
  if (!capture.begin()) return false;
  capture_ = &capture;
  portENTER_CRITICAL(&mux_);
  memset(sampleRing_, 0, sizeof(sampleRing_));
  memset(waterfall_, 0, sizeof(waterfall_));
  samplesSeen_ = lastFrameAt_ = 0;
  nextRow_ = rowsFilled_ = 0;
  portEXIT_CRITICAL(&mux_);
  if (!capture_->subscribe(this, onAudioSamples)) {
    capture_ = nullptr;
    return false;
  }
  active_ = true;
  return true;
#endif
}

void FmAudioFftService::exit() {
  active_ = false;
  if (capture_) capture_->unsubscribe(this);
  capture_ = nullptr;
}

void FmAudioFftService::onAudioSamples(void* context, const int16_t* samples, size_t count, int64_t utcStartMicros) {
  (void)utcStartMicros;
  if (!context || !samples) return;
  static_cast<FmAudioFftService*>(context)->pushSamples(samples, count);
}

void FmAudioFftService::pushSamples(const int16_t* samples, size_t count) {
  if (!active_ || !samples) return;
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < count; ++i) {
    sampleRing_[samplesSeen_ % 512] = samples[i];
    ++samplesSeen_;
  }
  portEXIT_CRITICAL(&mux_);
}

void FmAudioFftService::update(int16_t rssi) {
  if (!active_) return;
  uint32_t seen = 0;
  portENTER_CRITICAL(&mux_);
  seen = samplesSeen_;
  portEXIT_CRITICAL(&mux_);
  if (seen < kFftSize || seen - lastFrameAt_ < 64) return;
  lastFrameAt_ = seen;
  processFrame(rssi);
}

void FmAudioFftService::processFrame(int16_t rssi) {
  float real[kFftSize]{};
  float imag[kFftSize]{};
  portENTER_CRITICAL(&mux_);
  const uint32_t end = samplesSeen_;
  if (end < kFftSize) {
    portEXIT_CRITICAL(&mux_);
    return;
  }
  for (size_t i = 0; i < kFftSize; ++i) {
    const int16_t sample = sampleRing_[(end - kFftSize + i) % 512];
    const float window = 0.5f - 0.5f * cosf(6.28318530718f * static_cast<float>(i) / (kFftSize - 1));
    real[i] = (static_cast<float>(sample) - 2048.0f) * window;
  }
  portEXIT_CRITICAL(&mux_);

  // In-place radix-2 Cooley-Tukey FFT.
  size_t j = 0;
  for (size_t i = 1; i < kFftSize; ++i) {
    size_t bit = kFftSize >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      const float tr = real[i]; real[i] = real[j]; real[j] = tr;
      const float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
    }
  }
  for (size_t len = 2; len <= kFftSize; len <<= 1) {
    const float angle = -6.28318530718f / static_cast<float>(len);
    const float stepR = cosf(angle), stepI = sinf(angle);
    for (size_t base = 0; base < kFftSize; base += len) {
      float wr = 1.0f, wi = 0.0f;
      for (size_t k = 0; k < len / 2; ++k) {
        const size_t even = base + k, odd = even + len / 2;
        const float tr = wr * real[odd] - wi * imag[odd];
        const float ti = wr * imag[odd] + wi * real[odd];
        real[odd] = real[even] - tr; imag[odd] = imag[even] - ti;
        real[even] += tr; imag[even] += ti;
        const float nextWr = wr * stepR - wi * stepI;
        wi = wr * stepI + wi * stepR; wr = nextWr;
      }
    }
  }

  const float rssiGain = rssi < 0 ? 0.70f : (0.60f + 0.40f * constrain(static_cast<float>(rssi), 0.0f, 100.0f) / 100.0f);
  uint8_t row[kBins]{};
  for (size_t bin = 0; bin < kBins; ++bin) {
    const size_t first = 1 + bin * 2;
    const float m1 = sqrtf(real[first] * real[first] + imag[first] * imag[first]);
    const float m2 = sqrtf(real[first + 1] * real[first + 1] + imag[first + 1] * imag[first + 1]);
    const float magnitude = (m1 > m2 ? m1 : m2) / 256.0f;
    const float db = 20.0f * log10f(magnitude + 0.5f);
    const float scaled = (db + 6.0f) * 4.0f * rssiGain;
    row[bin] = static_cast<uint8_t>(constrain(static_cast<int>(scaled), 0, 255));
  }

  portENTER_CRITICAL(&mux_);
  memcpy(waterfall_[nextRow_], row, sizeof(row));
  nextRow_ = static_cast<uint8_t>((nextRow_ + 1) % kRows);
  if (rowsFilled_ < kRows) ++rowsFilled_;
  portEXIT_CRITICAL(&mux_);
}

void FmAudioFftService::copyWaterfall(uint8_t* destination) const {
  if (!destination) return;
  portENTER_CRITICAL(&mux_);
  const uint8_t first = rowsFilled_ == kRows ? nextRow_ : 0;
  for (uint8_t row = 0; row < kRows; ++row) {
    const uint8_t source = static_cast<uint8_t>((first + row) % kRows);
    if (row < rowsFilled_) memcpy(destination + static_cast<size_t>(row) * kBins, waterfall_[source], kBins);
    else memset(destination + static_cast<size_t>(row) * kBins, 0, kBins);
  }
  portEXIT_CRITICAL(&mux_);
}
