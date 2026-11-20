#include "WifiScanner.h"
#include <string.h>

bool WifiScanner::begin(const char* const* ssids, uint8_t count, bool includeHidden) {
  stop();
  if (!ssids || !count) return false;
  if (count > MAX_NETWORKS) count = MAX_NETWORKS;
  memset(candidates_, 0, sizeof(candidates_));
  memset(found_, 0, sizeof(found_));
  memset(tried_, 0, sizeof(tried_));
  for (uint8_t i = 0; i < MAX_NETWORKS; ++i)
    ssids_[i] = i < count ? ssids[i] : nullptr;
  const int result = WiFi.scanNetworks(true, includeHidden);
  if (result == WIFI_SCAN_FAILED) return false;
  scanning_ = true;
  return true;
}

bool WifiScanner::update() {
  if (!scanning_) return ready_;
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) return false;
  scanning_ = false;
  ready_ = count >= 0;
  if (ready_) {
    for (uint8_t network = 0; network < MAX_NETWORKS; ++network) {
      if (!ssids_[network] || !ssids_[network][0]) continue;
      for (int i = 0; i < count; ++i) {
        if (WiFi.SSID(i) != ssids_[network]) continue;
        if (found_[network] && WiFi.RSSI(i) <= candidates_[network].rssi) continue;
        Candidate& candidate = candidates_[network];
        candidate.networkIndex = network;
        candidate.rssi = WiFi.RSSI(i);
        candidate.channel = WiFi.channel(i);
        candidate.open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        const uint8_t* bssid = WiFi.BSSID(i);
        if (bssid) memcpy(candidate.bssid, bssid, sizeof(candidate.bssid));
        found_[network] = candidate.channel > 0 && bssid != nullptr;
      }
    }
  }
  WiFi.scanDelete();
  return true;
}

bool WifiScanner::next(Candidate& candidate) {
  if (!ready_) return false;
  int8_t best = -1;
  for (uint8_t i = 0; i < MAX_NETWORKS; ++i) {
    if (!found_[i] || tried_[i]) continue;
    if (best < 0 || candidates_[i].rssi > candidates_[best].rssi)
      best = static_cast<int8_t>(i);
  }
  if (best < 0) return false;
  tried_[best] = true;
  candidate = candidates_[best];
  return true;
}

void WifiScanner::stop() {
  if (scanning_) WiFi.scanDelete();
  scanning_ = false;
  ready_ = false;
  memset(found_, 0, sizeof(found_));
  memset(tried_, 0, sizeof(tried_));
}
