#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

class AudioCaptureService {
public:
  static constexpr uint8_t kPin = 11;
  static constexpr uint32_t kSampleRate = 4000;
  static constexpr size_t kBlockSamples = 24;
  using Consumer = void (*)(void* context, const int16_t* samples, size_t count, int64_t utcStartMicros);

  bool begin();
  bool subscribe(void* context, Consumer consumer);
  void unsubscribe(void* context);
  bool running() const { return initialized_; }

private:
  struct Subscriber { void* context = nullptr; Consumer consumer = nullptr; };
  static constexpr size_t kMaxSubscribers = 4;
  static void taskEntry(void* arg);
  void sampleTask();
  size_t snapshotSubscribers(Subscriber* out, size_t capacity);

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  Subscriber subscribers_[kMaxSubscribers]{};
  bool initialized_ = false;
};
