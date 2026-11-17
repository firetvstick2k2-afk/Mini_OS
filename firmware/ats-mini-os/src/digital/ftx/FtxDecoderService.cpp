#include "FtxDecoderService.h"

#include <string.h>
#include "../../system/BootDiagnostics.h"

namespace {
constexpr size_t kAudioQueueDepth = 160;
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr UBaseType_t kWorkerPriority = 2;
constexpr BaseType_t kWorkerCore = 1;
constexpr int64_t kBlockDurationUs =
  static_cast<int64_t>(AudioCaptureService::kBlockSamples) * 1000000LL /
  AudioCaptureService::kSampleRate;
}

bool FtxDecoderService::begin(AudioCaptureService& capture) {
  if (initialized_) return capture_ == &capture;
  queue_ = xQueueCreate(kAudioQueueDepth, sizeof(AudioChunk));
  engineMutex_ = xSemaphoreCreateMutex();
  resultMutex_ = xSemaphoreCreateMutex();
  if (!queue_ || !engineMutex_ || !resultMutex_) {
    if (queue_) vQueueDelete(queue_);
    if (engineMutex_) vSemaphoreDelete(engineMutex_);
    if (resultMutex_) vSemaphoreDelete(resultMutex_);
    queue_ = nullptr;
    engineMutex_ = resultMutex_ = nullptr;
    return false;
  }

  capture_ = &capture;
  if (xTaskCreatePinnedToCore(taskEntry, "ftx-decoder", kWorkerStackBytes,
                              this, kWorkerPriority, &task_, kWorkerCore) != pdPASS) {
    vQueueDelete(queue_);
    vSemaphoreDelete(engineMutex_);
    vSemaphoreDelete(resultMutex_);
    queue_ = nullptr;
    engineMutex_ = resultMutex_ = nullptr;
    task_ = nullptr;
    capture_ = nullptr;
    return false;
  }
  initialized_ = true;
  return true;
}

bool FtxDecoderService::enter(FtxMode mode, bool wifiOff, bool utcSynchronized, AudioCaptureService& capture) {
  exit();
#if defined(LILYGO_SI473X)
  (void)mode; (void)wifiOff; (void)utcSynchronized;
  return false; // This board profile uses GPIO11 for SPI and has no ATS Mini audio input.
#else
  if (!wifiOff || !utcSynchronized) return false;
  if (!capture.begin() || !begin(capture)) return false;

  xSemaphoreTake(engineMutex_, portMAX_DELAY);
  const bool ready = decoder_.begin(mode);
  if (ready) {
    mode_ = mode;
    collecting_ = false;
    activeSlotStartUs_ = 0;
    lastBlockStartUs_ = 0;
    droppedBlocks_ = 0;
    receivedBlocks_ = completedSlots_ = decodedSlots_ = 0;
    slotStartAttempts_ = slotStartSuccesses_ = slotStartFailures_ = 0;
    boundaryHits_ = timestampGaps_ = chunksProcessed_ = 0;
    lastChunkStartUs_ = lastChunkEndUs_ = lastBoundaryUs_ = lastChunkDeltaUs_ = 0;
    xSemaphoreTake(resultMutex_, portMAX_DELAY);
    memset(published_, 0, sizeof(published_));
    xSemaphoreGive(resultMutex_);
    xQueueReset(queue_);
    BootDiagnostics::printf("[FTX][START] mode=%s sample_rate=%u block_samples=%u block_us=%lld",
      mode_ == FtxMode::FT4 ? "FT4" : "FT8",
      static_cast<unsigned>(AudioCaptureService::kSampleRate),
      static_cast<unsigned>(AudioCaptureService::kBlockSamples),
      static_cast<long long>(kBlockDurationUs));
    BootDiagnostics::printf("[FTX][DSP] initialized=%u fft_block_size=%u wf_max_blocks=%u",
      decoder_.initialized() ? 1U : 0U, static_cast<unsigned>(decoder_.fftBlockSize()),
      static_cast<unsigned>(decoder_.waterfallMaxBlocks()));
  }
  xSemaphoreGive(engineMutex_);
  if (!ready) return false;

  active_ = true;
  if (!capture_->subscribe(this, onAudioSamples)) {
    active_ = false;
    xSemaphoreTake(engineMutex_, portMAX_DELAY);
    decoder_.end();
    xSemaphoreGive(engineMutex_);
    BootDiagnostics::write("[FTX][START_FAIL] audio subscriber registration failed");
    return false;
  }
  return true;
#endif
}

void FtxDecoderService::exit() {
  active_ = false;
  if (capture_) capture_->unsubscribe(this);
  if (queue_) xQueueReset(queue_);
  if (engineMutex_ && initialized_) {
    xSemaphoreTake(engineMutex_, portMAX_DELAY);
    collecting_ = false;
    activeSlotStartUs_ = 0;
    lastBlockStartUs_ = 0;
    decoder_.end();
    xSemaphoreGive(engineMutex_);
  }
}

size_t FtxDecoderService::copyResults(FtxDecodedMessage* out, size_t capacity) {
  if (!out || capacity == 0 || !resultMutex_) return 0;
  xSemaphoreTake(resultMutex_, portMAX_DELAY);
  size_t count = capacity < kResultCapacity ? capacity : kResultCapacity;
  size_t available = 0;
  while (available < kResultCapacity && published_[available].text[0]) ++available;
  if (count > available) count = available;
  memcpy(out, published_, count * sizeof(FtxDecodedMessage));
  xSemaphoreGive(resultMutex_);
  return count;
}

void FtxDecoderService::onAudioSamples(void* context, const int16_t* samples,
                                       size_t count, int64_t utcStartMicros) {
  auto* self = static_cast<FtxDecoderService*>(context);
  if (!self || !self->active_ || !self->queue_ || !samples ||
      count == 0 || count > AudioCaptureService::kBlockSamples) return;
  AudioChunk chunk{};
  memcpy(chunk.samples, samples, count * sizeof(int16_t));
  chunk.count = static_cast<uint8_t>(count);
  chunk.utcStartMicros = utcStartMicros;
  ++self->receivedBlocks_;
  if (xQueueSend(self->queue_, &chunk, 0) != pdTRUE) ++self->droppedBlocks_;
}

void FtxDecoderService::taskEntry(void* arg) {
  static_cast<FtxDecoderService*>(arg)->workerTask();
}

void FtxDecoderService::workerTask() {
  AudioChunk chunk{};
  for (;;) {
    if (xQueueReceive(queue_, &chunk, portMAX_DELAY) != pdTRUE) continue;
    if (!active_) continue;
    xSemaphoreTake(engineMutex_, portMAX_DELAY);
    if (active_) processChunk(chunk);
    xSemaphoreGive(engineMutex_);
  }
}

void FtxDecoderService::processChunk(const AudioChunk& chunk) {
  if (!active_ || chunk.count == 0) return;
  const int64_t chunkStart = chunk.utcStartMicros;
  const int64_t chunkEnd = chunkStart +
    static_cast<int64_t>(chunk.count) * 1000000LL / AudioCaptureService::kSampleRate;
  const int64_t slotLengthUs = mode_ == FtxMode::FT4 ? 7500000LL : 15000000LL;

  ++chunksProcessed_;
  lastChunkStartUs_ = chunkStart;
  lastChunkEndUs_ = chunkEnd;
  lastChunkDeltaUs_ = lastBlockStartUs_ ? chunkStart - lastBlockStartUs_ : 0;
  if (lastBlockStartUs_ && chunkStart - lastBlockStartUs_ > kBlockDurationUs * 2) {
    ++timestampGaps_;
    BootDiagnostics::printf("[FTX][TIMING][GAP] delta_us=%lld expected_us=%lld chunk_start=%lld previous_start=%lld collecting=%u slot_start=%lld",
      static_cast<long long>(chunkStart - lastBlockStartUs_), static_cast<long long>(kBlockDurationUs),
      static_cast<long long>(chunkStart), static_cast<long long>(lastBlockStartUs_),
      collecting_ ? 1U : 0U, static_cast<long long>(activeSlotStartUs_));
    collecting_ = false;
    activeSlotStartUs_ = 0;
  }
  lastBlockStartUs_ = chunkStart;

  const int64_t currentSlotStart = (chunkStart / slotLengthUs) * slotLengthUs;
  const int64_t nextBoundary = currentSlotStart + slotLengthUs;
  lastBoundaryUs_ = nextBoundary;

  if (collecting_ && activeSlotStartUs_ == currentSlotStart) {
    if (chunkEnd >= nextBoundary) {
      ++boundaryHits_;
      const int64_t previousSlotStart = activeSlotStartUs_;
      const int64_t untilBoundary = nextBoundary - chunkStart;
      size_t before = untilBoundary <= 0 ? 0 :
        static_cast<size_t>((untilBoundary * AudioCaptureService::kSampleRate + 999999LL) / 1000000LL);
      if (before > chunk.count) before = chunk.count;
      if (before) decoder_.pushSamples(chunk.samples, before);
      const bool readyAtBoundary = decoder_.slotReady();
      BootDiagnostics::printf("[FTX][SLOT][END] mode=%s slot_start=%lld boundary=%lld chunk_start=%lld chunk_end=%lld samples_in_chunk=%u samples_before=%u wf_blocks=%u/%u ready=%u frame_fill=%u",
        mode_ == FtxMode::FT4 ? "FT4" : "FT8", static_cast<long long>(previousSlotStart),
        static_cast<long long>(nextBoundary), static_cast<long long>(chunkStart),
        static_cast<long long>(chunkEnd), static_cast<unsigned>(chunk.count),
        static_cast<unsigned>(before), static_cast<unsigned>(decoder_.symbolsCollected()),
        static_cast<unsigned>(decoder_.waterfallMaxBlocks()), readyAtBoundary ? 1U : 0U,
        static_cast<unsigned>(decoder_.pendingFrameSamples()));
      if (readyAtBoundary) decodeAndPublish();
      collecting_ = false;
      activeSlotStartUs_ = 0;

      ++slotStartAttempts_;
      const bool nextStarted = decoder_.startSlot(static_cast<time_t>(nextBoundary / 1000000LL));
      if (nextStarted) {
        ++slotStartSuccesses_;
        activeSlotStartUs_ = nextBoundary;
        collecting_ = true;
        BootDiagnostics::printf("[FTX][SLOT][START] mode=%s utc=%lld source=boundary-split",
          mode_ == FtxMode::FT4 ? "FT4" : "FT8", static_cast<long long>(nextBoundary / 1000000LL));
        if (before < chunk.count) {
          decoder_.pushSamples(chunk.samples + before, chunk.count - before);
          if (decoder_.slotReady()) {
            decodeAndPublish();
            collecting_ = false;
            activeSlotStartUs_ = 0;
          }
        }
      } else {
        ++slotStartFailures_;
        BootDiagnostics::printf("[FTX][SLOT][START_FAIL] mode=%s utc=%lld source=boundary-split",
          mode_ == FtxMode::FT4 ? "FT4" : "FT8", static_cast<long long>(nextBoundary / 1000000LL));
      }
      return;
    }

    decoder_.pushSamples(chunk.samples, chunk.count);
    if (decoder_.slotReady()) {
      decodeAndPublish();
      collecting_ = false;
      activeSlotStartUs_ = 0;
    }
    return;
  }

  // Wait for the exact UTC slot edge. A chunk that straddles the edge is split
  // at the first sample on/after that edge, preserving symbol timing.
  if (chunkStart < nextBoundary && chunkEnd >= nextBoundary) {
    ++boundaryHits_;
    const int64_t delta = nextBoundary - chunkStart;
    size_t offset = delta <= 0 ? 0 :
      static_cast<size_t>((delta * AudioCaptureService::kSampleRate + 999999LL) / 1000000LL);
    if (offset > chunk.count) {
      BootDiagnostics::printf("[FTX][SLOT][OFFSET_FAIL] offset=%u count=%u delta_us=%lld",
        static_cast<unsigned>(offset), static_cast<unsigned>(chunk.count), static_cast<long long>(delta));
      return;
    }
    ++slotStartAttempts_;
    if (!decoder_.startSlot(static_cast<time_t>(nextBoundary / 1000000LL))) {
      ++slotStartFailures_;
      BootDiagnostics::printf("[FTX][SLOT][START_FAIL] mode=%s utc=%lld source=first-boundary offset=%u count=%u",
        mode_ == FtxMode::FT4 ? "FT4" : "FT8", static_cast<long long>(nextBoundary / 1000000LL),
        static_cast<unsigned>(offset), static_cast<unsigned>(chunk.count));
      return;
    }
    ++slotStartSuccesses_;
    activeSlotStartUs_ = nextBoundary;
    collecting_ = true;
    BootDiagnostics::printf("[FTX][SLOT][START] mode=%s utc=%lld source=first-boundary offset=%u count=%u",
      mode_ == FtxMode::FT4 ? "FT4" : "FT8", static_cast<long long>(nextBoundary / 1000000LL),
      static_cast<unsigned>(offset), static_cast<unsigned>(chunk.count));
    decoder_.pushSamples(chunk.samples + offset, chunk.count - offset);
    if (decoder_.slotReady()) {
      decodeAndPublish();
      collecting_ = false;
      activeSlotStartUs_ = 0;
    }
  }
}

void FtxDecoderService::decodeAndPublish() {
  ++completedSlots_;
  const int blocksBeforeDecode = decoder_.symbolsCollected();
  const int maxBlocksBeforeDecode = decoder_.waterfallMaxBlocks();
  const int64_t slotStart = activeSlotStartUs_;
  decoder_.decodeSlot();
  BootDiagnostics::printf("[FTX][DECODE] mode=%s slot_start=%lld wf_blocks=%d/%d results=%u",
    mode_ == FtxMode::FT4 ? "FT4" : "FT8", static_cast<long long>(slotStart),
    blocksBeforeDecode, maxBlocksBeforeDecode, static_cast<unsigned>(decoder_.resultCount()));
  if (decoder_.resultCount() > 0) ++decodedSlots_;
  FtxDecodedMessage fresh[kResultCapacity]{};
  const size_t count = decoder_.resultCount() < kResultCapacity ?
    decoder_.resultCount() : kResultCapacity;
  for (size_t i = 0; i < count; ++i) fresh[i] = decoder_.result(i);

  xSemaphoreTake(resultMutex_, portMAX_DELAY);
  memcpy(published_, fresh, sizeof(published_));
  xSemaphoreGive(resultMutex_);
}

bool FtxDecoderService::copyWaterfall(uint8_t* out, uint16_t maxRows, uint16_t outBins,
                                      uint16_t& rows, uint16_t& bins) {
  rows = bins = 0;
  if (!active_ || !engineMutex_ ||
      xSemaphoreTake(engineMutex_, pdMS_TO_TICKS(5)) != pdTRUE) return false;
  const bool copied = decoder_.copyWaterfall(out, maxRows, outBins, rows, bins);
  xSemaphoreGive(engineMutex_);
  return copied;
}
