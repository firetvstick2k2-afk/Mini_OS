#pragma once
#include <Arduino.h>
#include "BandManager.h"

// Fixed receiver presets. SI4735 AM/SSB tuning is expressed in whole kHz;
// decimal-kHz carriers are rounded to the nearest receiver kHz.
struct DecoderFrequencyPreset {
  const char* label;
  uint32_t frequencyKHz;
  RadioModulation modulation;
  uint8_t bandwidthChoice;
};

static constexpr DecoderFrequencyPreset kDecoderFrequencyPresets[] = {
  // CW activity
  {"CW 160m QRP", 1836, RadioModulation::USB, 0},
  {"CW 80m QRP", 3560, RadioModulation::USB, 0},
  {"CW 40m QRP", 7030, RadioModulation::USB, 0},
  {"CW 30m QRP", 10116, RadioModulation::USB, 0},
  {"CW 20m QRP", 14060, RadioModulation::USB, 0},
  {"CW 17m", 18096, RadioModulation::USB, 0},
  {"CW 15m", 21060, RadioModulation::USB, 0},
  {"CW 12m", 24906, RadioModulation::USB, 0},
  {"CW 10m", 28060, RadioModulation::USB, 0},

  // FT8
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

  // FT4
  {"FT4 80m", 3575, RadioModulation::USB, 4},
  {"FT4 40m", 7048, RadioModulation::USB, 4},
  {"FT4 30m", 10140, RadioModulation::USB, 4},
  {"FT4 20m", 14080, RadioModulation::USB, 4},
  {"FT4 17m", 18104, RadioModulation::USB, 4},
  {"FT4 15m", 21140, RadioModulation::USB, 4},
  {"FT4 12m", 24919, RadioModulation::USB, 4},
  {"FT4 10m", 28180, RadioModulation::USB, 4},

  // RTTY
  {"RTTY 80m", 3580, RadioModulation::USB, 4},
  {"RTTY 40m", 7045, RadioModulation::USB, 4},
  {"RTTY 20m", 14080, RadioModulation::USB, 4},
  {"RTTY 15m", 21080, RadioModulation::USB, 4},
  {"RTTY 10m", 28080, RadioModulation::USB, 4},

  // DWD Pinneberg: RTTY/weather data and weather fax
  {"PINNEBERG DDH 47", 147, RadioModulation::AM, 4},
  {"PINNEBERG DDK 2", 4583, RadioModulation::USB, 4},
  {"PINNEBERG DDH 7", 7646, RadioModulation::USB, 4},
  {"PINNEBERG DDK 9", 10101, RadioModulation::USB, 4},
  {"PINNEBERG DDH 9", 11039, RadioModulation::USB, 4},
  {"PINNEBERG DDH 8", 14467, RadioModulation::USB, 4},
  {"DWD FAX 7.880", 7880, RadioModulation::USB, 4},
  {"DWD FAX 13.882", 13883, RadioModulation::USB, 4},
  {"DWD RTTY 3.855", 3855, RadioModulation::USB, 4},

  // NCDXF / IARU international propagation beacons
  {"NCDXF BEACON 20m", 14100, RadioModulation::USB, 4},
  {"NCDXF BEACON 17m", 18110, RadioModulation::USB, 4},
  {"NCDXF BEACON 15m", 21150, RadioModulation::USB, 4},
  {"NCDXF BEACON 12m", 24930, RadioModulation::USB, 4},
  {"NCDXF BEACON 10m", 28200, RadioModulation::USB, 4},

  // Time and frequency standards
  {"WWV / WWVH 2.5 MHz", 2500, RadioModulation::AM, 4},
  {"WWV / WWVH 5 MHz", 5000, RadioModulation::AM, 4},
  {"WWV / WWVH 10 MHz", 10000, RadioModulation::AM, 4},
  {"WWV / WWVH 15 MHz", 15000, RadioModulation::AM, 4},
  {"WWV 20 MHz", 20000, RadioModulation::AM, 4},
  {"WWV TEST 25 MHz", 25000, RadioModulation::AM, 4},

  // Shortwave broadcast / utility listening points
  {"KURZWELLE 3.950", 3950, RadioModulation::AM, 4},
  {"60m BC / UTILITY", 5000, RadioModulation::AM, 4},
  {"49m BROADCAST", 6000, RadioModulation::AM, 4},
  {"41m BROADCAST", 7300, RadioModulation::AM, 4},
  {"31m BROADCAST", 9500, RadioModulation::AM, 4},
  {"25m BROADCAST", 11800, RadioModulation::AM, 4},
  {"19m BROADCAST", 15100, RadioModulation::AM, 4},
  {"16m BROADCAST", 17500, RadioModulation::AM, 4},

  // Amateur SSB voice activity
  {"80m SSB QSO", 3700, RadioModulation::LSB, 4},
  {"40m SSB QSO", 7100, RadioModulation::LSB, 4},
  {"20m SSB QSO", 14200, RadioModulation::USB, 4},
  {"17m SSB QSO", 18130, RadioModulation::USB, 4},
  {"15m SSB QSO", 21285, RadioModulation::USB, 4},
  {"12m SSB QSO", 24950, RadioModulation::USB, 4},
  {"10m SSB QSO", 28400, RadioModulation::USB, 4},

  // Well-known voice / marker stations and signal-hunting spots
  {"UVB-76 / BUZZER", 4625, RadioModulation::USB, 4},
  {"V32 NUMBERS", 7910, RadioModulation::USB, 4},
  {"V32 ALTERNATE", 7842, RadioModulation::USB, 4},
  {"HF DATA HUNT 10.150", 10150, RadioModulation::USB, 4},
  {"HF DATA HUNT 14.070", 14070, RadioModulation::USB, 4},
  {"HF DATA HUNT 21.070", 21070, RadioModulation::USB, 4},

  // CB
  {"CB KANAL 24", 27235, RadioModulation::AM, 4},
  {"CB KANAL 25", 27245, RadioModulation::AM, 4},
};

static constexpr uint8_t kDecoderFrequencyPresetCount =
  sizeof(kDecoderFrequencyPresets) / sizeof(kDecoderFrequencyPresets[0]);
