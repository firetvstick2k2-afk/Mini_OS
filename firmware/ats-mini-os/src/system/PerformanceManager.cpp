#include "PerformanceManager.h"

void PerformanceManager::begin() {
  // Remember the board/core startup clock as the normal Auto baseline.
  autoClockMHz_ = getCpuFrequencyMhz();
  applyPolicy();
}

void PerformanceManager::update() {
  const uint32_t now = millis();
  bool changed = false;
  for (auto &lease : leases_) {
    if (lease.active && static_cast<int32_t>(now - lease.expiresAt) >= 0) {
      lease.active = false;
      changed = true;
    }
  }
  if (changed) applyPolicy();
}

bool PerformanceManager::setProfile(CpuProfile profile) {
  if (static_cast<uint8_t>(profile) > static_cast<uint8_t>(CpuProfile::FixedHigh)) {
    return false;
  }
  selectedProfile_ = profile;
  applyPolicy();
  return true;
}

const char* PerformanceManager::profileName() const {
  switch (selectedProfile_) {
    case CpuProfile::Auto: return "Auto";
    case CpuProfile::FixedLow: return "Taktstufe 1";
    case CpuProfile::FixedMedium: return "Taktstufe 2";
    case CpuProfile::FixedHigh: return "Taktstufe 3";
  }
  return "Unknown";
}

bool PerformanceManager::request(PerformanceReason reason, uint32_t timeoutMs) {
  const auto index = static_cast<uint8_t>(reason);
  if (index >= static_cast<uint8_t>(PerformanceReason::Count) || timeoutMs == 0) {
    return false;
  }
  // A waterfall boost is a hard override, including in fixed-profile mode.
  if (reason == PerformanceReason::Waterfall) {
    setWaterfallActive(true);
    return true;
  }
  if (selectedProfile_ == CpuProfile::Auto) {
    leases_[index].active = true;
    leases_[index].expiresAt = millis() + timeoutMs;
    applyPolicy();
  }
  return true;
}

void PerformanceManager::release(PerformanceReason reason) {
  if (reason == PerformanceReason::Waterfall) {
    setWaterfallActive(false);
    return;
  }
  const auto index = static_cast<uint8_t>(reason);
  if (index >= static_cast<uint8_t>(PerformanceReason::Count)) return;
  if (leases_[index].active) {
    leases_[index].active = false;
    applyPolicy();
  }
}

void PerformanceManager::setWaterfallActive(bool active) {
  if (waterfallActive_ == active) return;
  waterfallActive_ = active;
  applyPolicy();
}

bool PerformanceManager::anyLeaseActive() const {
  for (const auto &lease : leases_) {
    if (lease.active) return true;
  }
  return false;
}

void PerformanceManager::applyPolicy() {
  // The waterfall uses 160 MHz as a lower-power performance compromise.
  // Leaving it restores the user's selected fixed profile, or the startup
  // clock when Auto is selected.
  uint32_t targetMHz = autoClockMHz_ ? autoClockMHz_ : 240;
  if (waterfallActive_) targetMHz = 160;
  else {
    switch (selectedProfile_) {
      case CpuProfile::FixedLow: targetMHz = 80; break;
      case CpuProfile::FixedMedium: targetMHz = 160; break;
      case CpuProfile::FixedHigh: targetMHz = 240; break;
      case CpuProfile::Auto:
        if (anyLeaseActive()) targetMHz = 240;
        break;
    }
  }
  if (getCpuFrequencyMhz() != targetMHz) {
    const bool applied = setCpuFrequencyMhz(targetMHz);
    Serial.printf("[CPU] target=%lu MHz applied=%s waterfall=%s profile=%s\\n",
      static_cast<unsigned long>(targetMHz), applied ? "yes" : "no",
      waterfallActive_ ? "yes" : "no", profileName());
  }
}
