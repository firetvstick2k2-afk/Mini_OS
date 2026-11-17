#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "CwDecoder.h"
#include "../audio/AudioCaptureService.h"

class CwDecoderService {
public:
  bool begin(AudioCaptureService& capture);
  bool enter(AudioCaptureService& capture, bool wifiOff);
  void exit();
  bool active() const { return active_; }
  bool wifiBlocked() const { return wifiBlocked_; }
  void clearText();
  void update();
  const char* text() const { return shownText_; }
  bool signal() const { return signal_; }
  uint16_t audioLevel() const { return audioLevel_; }
  int wpm() const { return wpm_; }

private:
  static void onAudioSamples(void* context, const int16_t* samples, size_t count, int64_t utcStartMicros);
  void consumeSamples(const int16_t* samples, size_t count);

  static constexpr uint32_t kSampleRate = AudioCaptureService::kSampleRate;
  CwDecoder decoder_;
  AudioCaptureService* capture_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
  volatile bool active_ = false;
  bool initialized_ = false, wifiBlocked_ = false, signal_ = false;
  volatile uint16_t audioLevel_ = 0;
  float dcEstimate_ = 2048.0f, devSum_ = 0.0f;
  uint32_t devCount_ = 0;
  char shownText_[CwDecoder::TEXT_MAX + 1] = {};
  int wpm_ = 0;
  uint32_t lastUiMs_ = 0, lastToneMs_ = 0;
};
