#pragma once
#include <Arduino.h>
#include "BandManager.h"

// Fixed receiver presets. SI4735 AM/SSB tuning is expressed in whole kHz;
// decimal-kHz DWD carriers are rounded to the nearest receiver kHz.
struct DecoderFrequencyPreset {
  const char* label;
  uint32_t frequencyKHz;
  RadioModulation modulation;
  uint8_t bandwidthChoice;
};

static constexpr DecoderFrequencyPreset kDecoderFrequencyPresets[] = {
  {"CW 160m QRP", 1836, RadioModulation::USB, 0},
  {"CW 80m QRP", 3560, RadioModulation::USB, 0},
  {"CW 40m QRP", 7030, RadioModulation::USB, 0},
  {"CW 30m QRP", 10116, RadioModulation::USB, 0},
  {"CW 20m QRP", 14060, RadioModulation::USB, 0},
  {"CW 17m", 18096, RadioModulation::USB, 0},
  {"CW 15m", 21060, RadioModulation::USB, 0},
  {"CW 12m", 24906, RadioModulation::USB, 0},
  {"CW 10m", 28060, RadioModulation::USB, 0},

  {"FT8 160m", 1840, RadioModulation::USB, 4},
  {"FT8 80m", 3573, RadioModulation::USB, 4},
  {"FT8 60m", 5357, RadioModulation::USB, 4},
  {"FT8 40m", 7074, RadioModulation::USB, 4},
  {"FT8 30m", 10136, RadioModulation::USB, 4},
  {"FT8 20m", 14074, RadioModulation::USB, 4},
  {"FT8 17m", 18100, RadioModulation::USB, 4},
  {"FT8 15m", 21074, RadioModulation::USB, 4},
  {"FT8 12m", 24915, RadioModulation::USB, 4},
  {"FT8 10m", 28074, RadioModulation::USB, 4},

  {"FT4 80m", 3575, RadioModulation::USB, 4},
  {"FT4 40m", 7047, RadioModulation::USB, 4},
  {"FT4 30m", 10140, RadioModulation::USB, 4},
  {"FT4 20m", 14080, RadioModulation::USB, 4},
  {"FT4 17m", 18104, RadioModulation::USB, 4},
  {"FT4 15m", 21140, RadioModulation::USB, 4},
  {"FT4 12m", 24919, RadioModulation::USB, 4},
  {"FT4 10m", 28180, RadioModulation::USB, 4},

  {"RTTY 80m", 3580, RadioModulation::USB, 4},
  {"RTTY 40m", 7045, RadioModulation::USB, 4},
  {"RTTY 20m", 14080, RadioModulation::USB, 4},
  {"RTTY 15m", 21080, RadioModulation::USB, 4},
  {"RTTY 10m", 28080, RadioModulation::USB, 4},

  {"PINNEBERG DDH 47", 147, RadioModulation::AM, 4},
  {"PINNEBERG DDK 2", 4583, RadioModulation::USB, 4},
  {"PINNEBERG DDH 7", 7646, RadioModulation::USB, 4},
  {"PINNEBERG DDK 9", 10101, RadioModulation::USB, 4},
  {"PINNEBERG DDH 9", 11039, RadioModulation::USB, 4},
  {"PINNEBERG DDH 8", 14467, RadioModulation::USB, 4},

  {"CB KANAL 24", 27235, RadioModulation::AM, 4},
  {"CB KANAL 25", 27245, RadioModulation::AM, 4},
};

static constexpr uint8_t kDecoderFrequencyPresetCount =
  sizeof(kDecoderFrequencyPresets) / sizeof(kDecoderFrequencyPresets[0]);
