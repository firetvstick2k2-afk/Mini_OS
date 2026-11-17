#include "AudioCaptureService.h"

#include <esp_timer.h>
#include <math.h>

bool AudioCaptureService::begin() {
  if (initialized_) return true;
  pinMode(kPin, INPUT);
  if (xTaskCreatePinnedToCore(taskEntry, "audio-capture", 4096, this, 3, nullptr, 0) != pdPASS) {
    return false;
  }
  initialized_ = true;
  return true;
}

bool AudioCaptureService::subscribe(void* context, Consumer consumer) {
  if (!initialized_ || !context || !consumer) return false;
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < kMaxSubscribers; ++i) {
    if (subscribers_[i].context == context) {
      subscribers_[i].consumer = consumer;
      portEXIT_CRITICAL(&mux_);
      return true;
    }
  }
  for (size_t i = 0; i < kMaxSubscribers; ++i) {
    if (!subscribers_[i].context) {
      subscribers_[i] = {context, consumer};
      portEXIT_CRITICAL(&mux_);
      return true;
    }
  }
  portEXIT_CRITICAL(&mux_);
  return false;
}

void AudioCaptureService::unsubscribe(void* context) {
  if (!context) return;
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < kMaxSubscribers; ++i) {
    if (subscribers_[i].context == context) subscribers_[i] = {};
  }
  portEXIT_CRITICAL(&mux_);
}

size_t AudioCaptureService::snapshotSubscribers(Subscriber* out, size_t capacity) {
  size_t count = 0;
  portENTER_CRITICAL(&mux_);
  for (size_t i = 0; i < kMaxSubscribers && count < capacity; ++i) {
    if (subscribers_[i].context && subscribers_[i].consumer) out[count++] = subscribers_[i];
  }
  portEXIT_CRITICAL(&mux_);
  return count;
}

void AudioCaptureService::taskEntry(void* arg) {
  static_cast<AudioCaptureService*>(arg)->sampleTask();
}

void AudioCaptureService::sampleTask() {
  int16_t samples[kBlockSamples];
  size_t count = 0;
  int64_t base = 0, next = 0;
  uint64_t index = 0;

  for (;;) {
    Subscriber active[kMaxSubscribers]{};
    if (snapshotSubscribers(active, kMaxSubscribers) == 0) {
      count = 0;
      base = next = 0;
      index = 0;
      vTaskDelay(pdMS_TO_TICKS(25));
      continue;
    }

    if (!base) {
      base = next = esp_timer_get_time();
      index = 0;
    }
    while (esp_timer_get_time() < next) taskYIELD();

    ++index;
    next = base + static_cast<int64_t>(index * 1000000ULL / kSampleRate);
    int value = analogRead(kPin);
    if (value < 0) value = 2048;
    samples[count++] = static_cast<int16_t>(value);

    if (count >= kBlockSamples) {
      const size_t consumers = snapshotSubscribers(active, kMaxSubscribers);
      for (size_t i = 0; i < consumers; ++i) {
        active[i].consumer(active[i].context, samples, count);
      }
      count = 0;
    }

    if (index % (kSampleRate / 2) == 0) {
      vTaskDelay(1);
      base = next = esp_timer_get_time();
      index = 0;
    }
    if (esp_timer_get_time() - next > 10000) {
      base = next = esp_timer_get_time();
      index = 0;
    }
  }
}
