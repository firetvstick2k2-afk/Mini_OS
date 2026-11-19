#pragma once
#include <Arduino.h>

class BandOpeningService {
public:
  enum class Status : uint8_t { Unknown = 0, Poor, Fair, Good };
  static constexpr uint8_t kBandsPerPage = 2;
  static constexpr uint8_t kBandCount = 29;
  static constexpr uint8_t kPageCount = (kBandCount + kBandsPerPage - 1) / kBandsPerPage;
  struct Band {
    const char* name;
    const char* type;
    uint32_t minimumKHz;
    uint32_t maximumKHz;
    uint16_t bandMeters; // zero means no directly comparable Map For HAM band value
  };
  struct Row {
    Status day = Status::Unknown;
    Status night = Status::Unknown;
  };
  struct Snapshot {
    Row rows[kBandCount]{};
    uint32_t updatedAtMs = 0;
    char sourceUpdated[32] = {};
    bool valid = false;
    bool stale = true;
    bool fetching = false;
    bool waitingForWifi = true;
    bool fetchFailed = false;
    float fmMufMHz = 0.0f;
    bool fmMufValid = false;
    bool fmMufStale = true;
  };
  bool begin();
  void update(bool wifiConnected);
  Snapshot snapshot() const;
  static const Band& band(uint8_t index);
  static const char* statusText(Status status);
private:
  static void taskEntry(void* context);
  void taskLoop();
  bool fetchConditions(Snapshot& result);
  bool fetchFmMuf(float& value);
  void publish(const Snapshot& value);
  volatile bool wifiConnected_ = false;
  volatile bool refreshRequested_ = false;
  uint32_t lastAttemptMs_ = 0;
  uint32_t lastMufAttemptMs_ = 0;
  TaskHandle_t taskHandle_ = nullptr;
  mutable portMUX_TYPE snapshotMux_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot snapshot_{};
};
