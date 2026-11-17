#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "FtxDecoder.h"
#include "../../audio/AudioCaptureService.h"

class FtxDecoderService {
public:
  static constexpr size_t kResultCapacity = FtxDecoder::kMaxResults;

  bool begin(AudioCaptureService& capture);
  bool enter(FtxMode mode, bool wifiOff, bool utcSynchronized);
  void exit();
  bool active() const { return active_; }
  FtxMode mode() const { return mode_; }
  bool copyWaterfall(uint8_t* out, uint16_t maxRows, uint16_t outBins, uint16_t& rows, uint16_t& bins);
  uint32_t droppedBlocks() const { return droppedBlocks_; }
  size_t copyResults(FtxDecodedMessage* out, size_t capacity);

private:
  struct AudioChunk {
    int16_t samples[AudioCaptureService::kBlockSamples];
    int64_t utcStartMicros = 0;
    uint8_t count = 0;
  };

  static void onAudioSamples(void* context, const int16_t* samples, size_t count, int64_t utcStartMicros);
  static void taskEntry(void* arg);
  void workerTask();
  void processChunk(const AudioChunk& chunk);
  void decodeAndPublish();

  AudioCaptureService* capture_ = nullptr;
  QueueHandle_t queue_ = nullptr;
  SemaphoreHandle_t engineMutex_ = nullptr;
  SemaphoreHandle_t resultMutex_ = nullptr;
  TaskHandle_t task_ = nullptr;
  FtxDecoder decoder_;
  FtxDecodedMessage published_[kResultCapacity]{};
  FtxMode mode_ = FtxMode::FT8;
  volatile bool active_ = false;
  bool initialized_ = false;
  bool collecting_ = false;
  int64_t activeSlotStartUs_ = 0;
  int64_t lastBlockStartUs_ = 0;
  volatile uint32_t droppedBlocks_ = 0;
};
