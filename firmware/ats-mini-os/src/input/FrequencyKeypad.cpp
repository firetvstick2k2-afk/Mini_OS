#include "FrequencyKeypad.h"
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint8_t kFm = 0;
constexpr uint8_t kLsb = 1;
constexpr uint8_t kUsb = 2;
constexpr uint8_t kAm = 3;
constexpr char* const kKeys[16] = {
  const_cast<char*>("1"), const_cast<char*>("2"), const_cast<char*>("3"), const_cast<char*>("BS"),
  const_cast<char*>("4"), const_cast<char*>("5"), const_cast<char*>("6"), const_cast<char*>("0"),
  const_cast<char*>("7"), const_cast<char*>("8"), const_cast<char*>("9"), const_cast<char*>("."),
  const_cast<char*>("OK"), const_cast<char*>("UNIT"), const_cast<char*>(""), const_cast<char*>("CLOSE")
};

RadioModulation toRadioMode(uint8_t mode) {
  switch (mode) {
    case kFm: return RadioModulation::FM;
    case kLsb: return RadioModulation::LSB;
    case kUsb: return RadioModulation::USB;
    default: return RadioModulation::AM;
  }
}

bool applyFrequency(RadioCore& radio, uint32_t frequencyKHz, RadioModulation mode) {
  int8_t selectedBand = -1;
  // Prefer a band whose preset modulation already matches the requested mode.
  for (uint8_t i = 0; i < BandManager::count(); ++i) {
    const RadioBandDefinition& band = BandManager::at(i);
    if (frequencyKHz < band.minimumKHz || frequencyKHz > band.maximumKHz) continue;
    if (mode == RadioModulation::FM) {
      if (band.type == RadioBandType::FM) { selectedBand = static_cast<int8_t>(i); break; }
    } else if (band.type != RadioBandType::FM && band.modulation == mode) {
      selectedBand = static_cast<int8_t>(i);
      break;
    } else if (band.type != RadioBandType::FM && selectedBand < 0) {
      selectedBand = static_cast<int8_t>(i);
    }
  }
  if (selectedBand < 0 || !radio.selectBand(static_cast<uint8_t>(selectedBand))) return false;
  if (radio.snapshot().modulation != mode && !radio.setModulation(mode)) return false;
  return radio.tune(frequencyKHz);
}
}

void FrequencyKeypad::clearInput() {
  memset(view_.input, 0, sizeof(view_.input));
  inputLength_ = 0;
}

void FrequencyKeypad::open(const RadioSnapshot& radioState) {
  clearInput();
  view_.index = 5; // Start on the "5" key, like ATS Mini DEV.
  view_.mode = radioState.modulation == RadioModulation::FM ? kFm :
    radioState.modulation == RadioModulation::LSB ? kLsb :
    radioState.modulation == RadioModulation::USB ? kUsb : kAm;
  view_.inputInMHz = view_.mode == kFm;
  view_.rangeError = false;
  active_ = true;
}

void FrequencyKeypad::rotate(int16_t delta) {
  if (!active_ || !delta) return;
  const int8_t direction = delta > 0 ? 1 : -1;
  int16_t next = static_cast<int16_t>(view_.index) + direction;
  if (next < 0) next = 16;
  if (next > 16) next = 0;
  while (next == 14) {
    next += direction;
    if (next < 0) next = 16;
    if (next > 16) next = 0;
  }
  view_.index = static_cast<uint8_t>(next);
}

bool FrequencyKeypad::press(RadioCore& radio) {
  if (!active_) return false;
  view_.rangeError = false;

  if (view_.index == 15) {
    active_ = false;
    return false;
  }
  if (view_.index == 16) {
    view_.mode = static_cast<uint8_t>((view_.mode + 1) % 4);
    view_.inputInMHz = view_.mode == kFm;
    return true;
  }
  if (view_.index == 14) return true;

  const char* key = kKeys[view_.index];
  if (strcmp(key, "UNIT") == 0) {
    if (view_.mode != kFm) view_.inputInMHz = !view_.inputInMHz;
    else view_.inputInMHz = true;
    return true;
  }
  if (strcmp(key, "BS") == 0) {
    if (inputLength_) view_.input[--inputLength_] = '\0';
    return true;
  }
  if (strcmp(key, "OK") == 0) {
    if (!inputLength_) {
      active_ = false;
      return false;
    }
    const double entered = strtod(view_.input, nullptr);
    if (!(entered > 0.0)) {
      view_.rangeError = true;
      return true;
    }
    const uint32_t targetKHz = view_.mode == kFm || view_.inputInMHz
      ? static_cast<uint32_t>(entered * 1000.0 + 0.5)
      : static_cast<uint32_t>(strtoul(view_.input, nullptr, 10));
    if (!applyFrequency(radio, targetKHz, toRadioMode(view_.mode))) {
      view_.rangeError = true;
      return true;
    }
    active_ = false;
    return false;
  }

  if (inputLength_ < sizeof(view_.input) - 1 && key[0]) {
    view_.input[inputLength_++] = key[0];
    view_.input[inputLength_] = '\0';
  }
  return true;
}
