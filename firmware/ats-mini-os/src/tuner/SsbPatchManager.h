#pragma once
#include <Arduino.h>
#include "ssb_patch.h"

// Central owner of the embedded SI4735 SSB DSP patch.
// Only the verified patch bundled with this firmware is selectable today;
// additional patch variants must be added explicitly with compatible metadata.
class SsbPatchManager {
public:
  static constexpr const char* name() { return "Embedded SI4735 SSB"; }
  static constexpr uint8_t version() { return 2; }
  static constexpr size_t size() { return sizeof(ssb_patch_content); }

  template <typename Radio>
  static bool load(Radio& radio) {
    if (size() == 0) return false;
    radio.loadPatch(ssb_patch_content, size(), version());
    return true;
  }
};
