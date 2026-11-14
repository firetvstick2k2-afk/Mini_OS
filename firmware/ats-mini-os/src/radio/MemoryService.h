#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "RadioCore.h"

// Persistent 99-slot sender memory, deliberately independent from UI code.
class MemoryService {
public:
  static constexpr uint8_t kSlotCount = 99;
  struct Entry {
    uint32_t frequencyKHz = 0;
    uint8_t modulation = 0;
    uint8_t bandIndex = 0;
    char name[16] = {};
  };
  bool begin();
  const Entry& entry(uint8_t slot) const;
  bool save(uint8_t slot, const RadioSnapshot& radio, uint8_t bandIndex, const char* name = nullptr);
  bool erase(uint8_t slot);
  bool recall(uint8_t slot, RadioCore& radio) const;
  uint8_t count() const;
private:
  static constexpr uint32_t kMagic = 0x4D454D31;
  struct Store { uint32_t magic; Entry entries[kSlotCount]; };
  Preferences preferences_;
  Store store_{};
  bool ready_ = false;
  bool persist();
};
