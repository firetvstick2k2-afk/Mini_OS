#pragma once
#include <Arduino.h>
#include "ssb_patch.h"
#include "PatchBinaryData.h"

// Patch profile: 0 = original embedded SSB patch / stock AM,
// 1 = experimental V1 AM+SSB, 2 = experimental V2 AM+SSB.
class SsbPatchManager {
public:
  enum : uint8_t { Standard = 0, V1 = 1, V2 = 2 };
  static constexpr size_t kMaxPatchSize = 15744;

  static uint8_t profile() { return selectedProfile(); }
  static bool setProfile(uint8_t profile) {
    if (profile > V2) return false;
    selectedProfile() = profile;
    return true;
  }
  static const char* profileName(uint8_t profile) {
    switch (profile) {
      case V1: return "V1";
      case V2: return "V2";
      default: return "Standard";
    }
  }

  template <typename Radio>
  static bool loadSsb(Radio& radio) {
    if (profile() == Standard) {
      radio.loadPatch(ssb_patch_content, sizeof(ssb_patch_content), 2);
      return sizeof(ssb_patch_content) != 0;
    }
    return loadExperimental(radio, true);
  }

  template <typename Radio>
  static bool loadAm(Radio& radio) {
    if (profile() == Standard) return true; // stock AM needs no custom patch
    return loadExperimental(radio, false);
  }

private:
  static uint8_t& selectedProfile() {
    static uint8_t value = Standard;
    return value;
  }

  static bool validFraming(const uint8_t* data, size_t size) {
    if (!data || size < 24 || size > kMaxPatchSize || (size % 8) != 0) return false;
    uint16_t remaining = 0;
    bool terminal = false;
    for (size_t offset = 0; offset < size; offset += 8) {
      const uint8_t* row = data + offset;
      if (terminal) return false;
      if (!remaining) {
        if (row[0] != 0x15 || row[1] != 0) return false;
        remaining = static_cast<uint16_t>((static_cast<uint16_t>(row[2]) << 8) | row[3]);
        terminal = remaining == 0;
      } else {
        if (row[0] != 0x16) return false;
        const uint8_t used = remaining < 7 ? static_cast<uint8_t>(remaining) : 7;
        for (uint8_t i = static_cast<uint8_t>(used + 1); i < 8; ++i)
          if (row[i] != 0) return false;
        remaining = static_cast<uint16_t>(remaining - used);
      }
    }
    return terminal && remaining == 0;
  }

  template <typename Radio>
  static bool loadExperimental(Radio& radio, bool ssb) {
    static uint8_t patchBuffer[kMaxPatchSize];
    const char* encoded = PatchBinaryData::encoded(profile(), ssb);
    if (!encoded) return false;
    size_t decodedSize = 0;
    if (!PatchBinaryData::decodeBase64(encoded, patchBuffer,
                                       sizeof(patchBuffer), decodedSize)) {
      Serial.println("[PATCH] Base64 decode failed; refusing patch");
      return false;
    }
    const size_t expected = profile() == V1
      ? (ssb ? 10208U : 1200U)
      : (ssb ? 15744U : 1608U);
    if (decodedSize != expected || !validFraming(patchBuffer, decodedSize)) {
      Serial.printf("[PATCH] Invalid patch size: %u (expected %u)\\n",
                    static_cast<unsigned>(decodedSize),
                    static_cast<unsigned>(expected));
      return false;
    }
    if (ssb) radio.loadPatch(patchBuffer, decodedSize, 2);
    else radio.loadAMPatch(patchBuffer, static_cast<uint16_t>(decodedSize));
    Serial.printf("[PATCH] Loaded %s %s (%u bytes)\\n",
                  profileName(profile()), ssb ? "SSB" : "AM",
                  static_cast<unsigned>(decodedSize));
    return true;
  }
};
