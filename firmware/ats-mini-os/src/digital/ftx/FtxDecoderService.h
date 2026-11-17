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
  bool enter(FtxMode mode, bool wifiOff, bool utcSynchronized, AudioCaptureService& capture);
  void exit();
  bool active() const { return active_; }
  FtxMode mode() const { return mode_; }
  bool copyWaterfall(uint8_t* out, uint16_t maxRows, uint16_t outBins, uint16_t& rows, uint16_t& bins);
  uint32_t droppedBlocks() const { return droppedBlocks_; }
  uint32_t receivedBlocks() const { return receivedBlocks_; }
  uint32_t completedSlots() const { return completedSlots_; }
  uint32_t decodedSlots() const { return decodedSlots_; }
  uint32_t slotStartAttempts() const { return slotStartAttempts_; }
  uint32_t slotStartSuccesses() const { return slotStartSuccesses_; }
  uint32_t slotStartFailures() const { return slotStartFailures_; }
  uint32_t boundaryHits() const { return boundaryHits_; }
  uint32_t timestampGaps() const { return timestampGaps_; }
  uint32_t chunksProcessed() const { return chunksProcessed_; }
  int64_t lastChunkStartUs() const { return lastChunkStartUs_; }
  int64_t lastChunkEndUs() const { return lastChunkEndUs_; }
  int64_t lastBoundaryUs() const { return lastBoundaryUs_; }
  int64_t lastChunkDeltaUs() const { return lastChunkDeltaUs_; }
  uint32_t decoderBlocks() const { return decoder_.symbolsCollected(); }
  uint32_t decoderMaxBlocks() const { return static_cast<uint32_t>(decoder_.waterfallMaxBlocks()); }
  uint32_t decoderFrameFill() const { return static_cast<uint32_t>(decoder_.pendingFrameSamples()); }
  uint32_t decoderBlockSize() const { return static_cast<uint32_t>(decoder_.fftBlockSize()); }
  bool decoderCollecting() const { return decoder_.collecting(); }
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
  volatile uint32_t receivedBlocks_ = 0;
  volatile uint32_t completedSlots_ = 0;
  volatile uint32_t decodedSlots_ = 0;
  volatile uint32_t slotStartAttempts_ = 0;
  volatile uint32_t slotStartSuccesses_ = 0;
  volatile uint32_t slotStartFailures_ = 0;
  volatile uint32_t boundaryHits_ = 0;
  volatile uint32_t timestampGaps_ = 0;
  volatile uint32_t chunksProcessed_ = 0;
  volatile int64_t lastChunkStartUs_ = 0;
  volatile int64_t lastChunkEndUs_ = 0;
  volatile int64_t lastBoundaryUs_ = 0;
  volatile int64_t lastChunkDeltaUs_ = 0;
};
