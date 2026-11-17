#include "FtxDecoderService.h"

#include <string.h>

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
    xSemaphoreTake(resultMutex_, portMAX_DELAY);
    memset(published_, 0, sizeof(published_));
    xSemaphoreGive(resultMutex_);
    xQueueReset(queue_);
  }
  xSemaphoreGive(engineMutex_);
  if (!ready) return false;

  active_ = true;
  if (!capture_->subscribe(this, onAudioSamples)) {
    active_ = false;
    xSemaphoreTake(engineMutex_, portMAX_DELAY);
    decoder_.end();
    xSemaphoreGive(engineMutex_);
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

  if (lastBlockStartUs_ && chunkStart - lastBlockStartUs_ > kBlockDurationUs * 2) {
    collecting_ = false; // A queue overrun invalidates this slot; never decode a broken stream.
    activeSlotStartUs_ = 0;
  }
  lastBlockStartUs_ = chunkStart;

  const int64_t currentSlotStart = (chunkStart / slotLengthUs) * slotLengthUs;
  const int64_t nextBoundary = currentSlotStart + slotLengthUs;

  if (collecting_ && activeSlotStartUs_ == currentSlotStart) {
    if (chunkEnd >= nextBoundary) {
      const int64_t untilBoundary = nextBoundary - chunkStart;
      size_t before = untilBoundary <= 0 ? 0 :
        static_cast<size_t>((untilBoundary * AudioCaptureService::kSampleRate + 999999LL) / 1000000LL);
      if (before > chunk.count) before = chunk.count;
      if (before) decoder_.pushSamples(chunk.samples, before);
      if (decoder_.slotReady()) decodeAndPublish();
      collecting_ = false;
      activeSlotStartUs_ = 0;

      if (decoder_.startSlot(static_cast<time_t>(nextBoundary / 1000000LL))) {
        activeSlotStartUs_ = nextBoundary;
        collecting_ = true;
        if (before < chunk.count) {
          decoder_.pushSamples(chunk.samples + before, chunk.count - before);
          if (decoder_.slotReady()) {
            decodeAndPublish();
            collecting_ = false;
            activeSlotStartUs_ = 0;
          }
        }
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
    const int64_t delta = nextBoundary - chunkStart;
    size_t offset = delta <= 0 ? 0 :
      static_cast<size_t>((delta * AudioCaptureService::kSampleRate + 999999LL) / 1000000LL);
    if (offset > chunk.count) return;
    if (!decoder_.startSlot(static_cast<time_t>(nextBoundary / 1000000LL))) return;
    activeSlotStartUs_ = nextBoundary;
    collecting_ = true;
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
  decoder_.decodeSlot();
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
