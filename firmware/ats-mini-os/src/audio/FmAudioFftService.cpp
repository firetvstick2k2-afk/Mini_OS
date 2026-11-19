#include "FmAudioFftService.h"
#include <math.h>
#include <string.h>
#include "../system/BootDiagnostics.h"

bool FmAudioFftService::enter(AudioCaptureService& capture) {
  if (active_) return capture_ == &capture;
  BootDiagnostics::memorySnapshot("FM_FFT_ENTER_REQUEST");
  if (attempted_) return false;
  attempted_ = true;
#if defined(LILYGO_SI473X)
  (void)capture;
  return false;
#else
  if (!capture.begin()) {
    BootDiagnostics::write("[FM FFT][START_FAIL] audio capture begin failed");
    BootDiagnostics::memorySnapshot("FM_FFT_CAPTURE_BEGIN_FAIL");
    return false;
  }
  BootDiagnostics::memorySnapshot("FM_FFT_CAPTURE_READY");
  capture_ = &capture;
  portENTER_CRITICAL(&mux_);
  memset(sampleRing_, 0, sizeof(sampleRing_));
  memset(waterfall_, 0, sizeof(waterfall_));
  memset(smoothedBins_, 0, sizeof(smoothedBins_));
  samplesSeen_ = lastFrameAt_ = 0;
  framesProcessed_ = 0;
  lastFrequencyKHz_ = 0;
  holdRowsUntilMs_ = 0;
  nextRow_ = rowsFilled_ = 0;
  haveSmoothedBins_ = false;
  portEXIT_CRITICAL(&mux_);
  if (!capture_->subscribe(this, onAudioSamples)) {
    capture_ = nullptr;
    BootDiagnostics::write("[FM FFT][START_FAIL] audio capture subscribe failed");
    BootDiagnostics::memorySnapshot("FM_FFT_SUBSCRIBE_FAIL");
    return false;
  }
  active_ = true;
  BootDiagnostics::write("[FM FFT] live audio FFT waterfall active");
  BootDiagnostics::memorySnapshot("FM_FFT_ACTIVE");
  return true;
#endif
}

void FmAudioFftService::exit() {
  if (active_ || capture_) {
    BootDiagnostics::printf("[FM FFT] exit frames=%lu samples=%lu rows=%u",
      static_cast<unsigned long>(framesProcessed_), static_cast<unsigned long>(samplesSeen_),
      static_cast<unsigned>(rowsFilled_));
    BootDiagnostics::memorySnapshot("FM_FFT_EXIT");
  }
  active_ = false;
  if (capture_) capture_->unsubscribe(this);
  capture_ = nullptr;
  attempted_ = false;
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
    sampleRing_[samplesSeen_ % kFftSize] = samples[i];
    ++samplesSeen_;
  }
  portEXIT_CRITICAL(&mux_);
}

void FmAudioFftService::update(int16_t rssi, uint32_t frequencyKHz) {
  if (!active_) return;
  if (frequencyKHz && lastFrequencyKHz_ && frequencyKHz != lastFrequencyKHz_) {
    // Keep the previous picture visible while the tuner settles; don't draw
    // the muted retune transient as a full-width blank/noise band.
    holdRowsUntilMs_ = millis() + 220;
    haveSmoothedBins_ = false;
  }
  if (frequencyKHz) lastFrequencyKHz_ = frequencyKHz;

  uint32_t seen = 0;
  portENTER_CRITICAL(&mux_);
  seen = samplesSeen_;
  portEXIT_CRITICAL(&mux_);
  if (seen < kFftSize || seen - lastFrameAt_ < kSamplesPerRow) return;
  lastFrameAt_ = seen;
  processFrame(rssi);
}

void FmAudioFftService::processFrame(int16_t rssi) {
  (void)rssi; // RF RSSI must not be used as an audio-level control.
  float real[kFftSize]{};
  float imag[kFftSize]{};
  portENTER_CRITICAL(&mux_);
  const uint32_t end = samplesSeen_;
  if (end < kFftSize) {
    portEXIT_CRITICAL(&mux_);
    return;
  }

  // Remove ADC DC offset before the Hann window.
  float mean = 0.0f;
  for (size_t i = 0; i < kFftSize; ++i)
    mean += static_cast<float>(sampleRing_[(end - kFftSize + i) % kFftSize]);
  mean /= static_cast<float>(kFftSize);
  for (size_t i = 0; i < kFftSize; ++i) {
    const float sample = static_cast<float>(sampleRing_[(end - kFftSize + i) % kFftSize]) - mean;
    const float window = 0.5f - 0.5f * cosf(6.28318530718f * static_cast<float>(i) / (kFftSize - 1));
    real[i] = sample * window;
  }
  portEXIT_CRITICAL(&mux_);

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

  uint8_t row[kBins]{};
  for (size_t bin = 0; bin < kBins; ++bin) {
    const size_t first = 1 + bin * 4;
    float power = 0.0f;
    for (size_t n = 0; n < 4; ++n)
      power += real[first + n] * real[first + n] + imag[first + n] * imag[first + n];
    // Convert the FFT magnitude back to a fraction of ADC full scale.
    // The input samples are raw 12-bit ADC readings (0..4095), not
    // signed 16-bit audio. A Hann window has a coherent gain of ~0.5;
    // for a sinusoid its FFT peak is therefore approximately A*N/4.
    // Averaging four neighbouring bins keeps the display less speckled.
    const float magnitude = sqrtf(power * 0.25f) /
                            (static_cast<float>(kFftSize) * 0.25f * 2048.0f);
    const float dbfs = 20.0f * log10f(magnitude + 0.000003f);

    // Display range: -84 dBFS (near black) to -18 dBFS (bright).
    // This leaves ordinary low-level noise dark instead of clipping
    // almost every bin into the white end of the palette.
    const float target = constrain((dbfs + 84.0f) * (255.0f / 66.0f), 0.0f, 255.0f);
    if (!haveSmoothedBins_) smoothedBins_[bin] = target;
    else {
      const float alpha = target > smoothedBins_[bin] ? 0.48f : 0.20f;
      smoothedBins_[bin] += (target - smoothedBins_[bin]) * alpha;
    }
    row[bin] = static_cast<uint8_t>(constrain(static_cast<int>(smoothedBins_[bin] + 0.5f), 0, 255));
  }
  haveSmoothedBins_ = true;

  // Freeze the existing waterfall during the brief RF retune gap.
  if (static_cast<int32_t>(millis() - holdRowsUntilMs_) < 0) return;

  portENTER_CRITICAL(&mux_);
  memcpy(waterfall_[nextRow_], row, sizeof(row));
  nextRow_ = static_cast<uint8_t>((nextRow_ + 1) % kRows);
  if (rowsFilled_ < kRows) ++rowsFilled_;
  portEXIT_CRITICAL(&mux_);
  ++framesProcessed_;
  BootDiagnostics::memorySnapshot("FM_FFT_FRAME");
  BootDiagnostics::printf("[FM FFT][FRAME] frame=%lu samples=%lu rows=%u next_row=%u bins=%u fft_stack_bytes=%u",
    static_cast<unsigned long>(framesProcessed_), static_cast<unsigned long>(samplesSeen_),
    static_cast<unsigned>(rowsFilled_), static_cast<unsigned>(nextRow_),
    static_cast<unsigned>(kBins), static_cast<unsigned>(sizeof(float) * kFftSize * 2U + kBins));
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
