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
    if (decodedSize != expected || decodedSize < 24 ||
        (decodedSize % 8) != 0) {
      Serial.printf("[PATCH] Invalid patch size: %u (expected %u)\\n",
                    static_cast<unsigned>(decodedSize),
                    static_cast<unsigned>(expected));
      return false;
    }
    radio.loadPatch(patchBuffer, decodedSize, 2);
    Serial.printf("[PATCH] Loaded %s %s (%u bytes)\\n",
                  profileName(profile()), ssb ? "SSB" : "AM",
                  static_cast<unsigned>(decodedSize));
    return true;
  }
};
