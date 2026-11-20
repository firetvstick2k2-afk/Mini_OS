#pragma once
#include <Arduino.h>
#include <WiFi.h>

// Owns asynchronous scan results and strongest-AP selection only.
class WifiScanner {
public:
  struct Candidate {
    uint8_t networkIndex = 0;
    int32_t rssi = INT32_MIN;
    int32_t channel = 0;
    uint8_t bssid[6] = {};
    bool open = false;
  };
  bool begin(const char* const* ssids, uint8_t count, bool includeHidden = false);
  bool update();
  bool next(Candidate& candidate);
  void stop();
  bool scanning() const { return scanning_; }
  bool ready() const { return ready_; }
private:
  static constexpr uint8_t MAX_NETWORKS = 3;
  const char* ssids_[MAX_NETWORKS] = {};
  Candidate candidates_[MAX_NETWORKS] = {};
  bool found_[MAX_NETWORKS] = {};
  bool tried_[MAX_NETWORKS] = {};
  bool scanning_ = false;
  bool ready_ = false;
};
