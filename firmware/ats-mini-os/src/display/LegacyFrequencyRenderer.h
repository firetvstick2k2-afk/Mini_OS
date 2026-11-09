#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "../radio/RadioSnapshot.h"

// Source-faithful Mini OS adaptation of ATS Mini DEV Draw.cpp::drawFrequency().
// Digit order, underline geometry, font selection, datum and unit placement
// follow the original renderer. Layouts supply their own coordinates/palette.
namespace LegacyFrequencyRenderer {

struct Line { int x, y, w; };

template <typename Canvas>
void draw(Canvas& spr,
          uint32_t freq,
          RadioModulation modulation,
          int x, int y, int ux, int uy,
          uint8_t hl,
          int16_t currentBFO,
          uint16_t frequencyColor,
          uint16_t normalHighlightColor,
          uint16_t selectedHighlightColor,
          uint16_t normalUnitColor = 0x07E0,
          bool showUnit = true) {
  static uint32_t lastFreq = 0;
  static uint32_t lastTuneTime = 0;

  if (freq != lastFreq) {
    lastFreq = freq;
    lastTuneTime = millis();
  }
  const uint16_t unitColor = ((millis() - lastTuneTime) < 800)
    ? 0xFFE0 : normalUnitColor;

  // Keep the original ATS Mini DEV cursor geometry verbatim.
  const Line hlDigitsFM[] = {
    { x - 30 - 32 * 0 -  0, y + 28, 27 },
    { x - 30 - 32 * 0 - 16, y + 28, 27 + 16 },
    { x - 30 - 32 * 1 -  0, y + 28, 27 },
    { x - 30 - 32 * 1 - 22, y + 28, 27 + 22 },
    { x - 30 - 32 * 2 - 12, y + 28, 27 },
    { x - 30 - 32 * 2 - 28, y + 28, 27 + 16 },
    { x - 30 - 32 * 3 - 12, y + 28, 27 },
    { x - 30 - 32 * 3 - 28, y + 28, 27 + 16 },
    { x - 30 - 32 * 4 +  4, y + 28, 11 },
  };

  const Line hlDigitsAMSSB[] = {
    { x + 12 + 14 * 2 -  0, y + 28, 12 },
    { x + 12 + 14 * 2 -  7, y + 28, 12 + 7 },
    { x + 12 + 14 * 1 -  0, y + 28, 12 },
    { x + 12 + 14 * 1 -  7, y + 28, 12 + 7 },
    { x + 12 + 14 * 0 -  0, y + 28, 12 },
    { x + 12 + 14 * 0 - 11, y + 28, 12 + 11 },
    { x - 30 - 32 * 0 -  0, y + 28, 27 },
    { x - 30 - 32 * 0 - 16, y + 28, 27 + 16 },
    { x - 30 - 32 * 1 -  0, y + 28, 27 },
    { x - 30 - 32 * 1 - 16, y + 28, 27 + 16 },
    { x - 30 - 32 * 2 -  0, y + 28, 27 },
    { x - 30 - 32 * 2 - 16, y + 28, 27 + 16 },
    { x - 30 - 32 * 3 -  0, y + 28, 27 },
    { x - 30 - 32 * 3 - 16, y + 28, 27 + 16 },
    { x - 30 - 32 * 4 -  0, y + 28, 27 },
  };

  const bool selectOn = (hl & 0x80) != 0;
  hl &= 0x7F;
  const bool isFm = modulation == RadioModulation::FM;
  const bool isSsb = modulation == RadioModulation::USB ||
                     modulation == RadioModulation::LSB;
  const Line* li = nullptr;
  if (isFm) {
    if (hl < sizeof(hlDigitsFM) / sizeof(hlDigitsFM[0]))
      li = &hlDigitsFM[hl];
  } else if (hl < sizeof(hlDigitsAMSSB) / sizeof(hlDigitsAMSSB[0])) {
    li = &hlDigitsAMSSB[hl];
  }

  spr.setTextDatum(MR_DATUM);
  spr.setTextColor(frequencyColor);
  if (isFm) {
    spr.drawFloat(freq / 1000.0f, 2, x, y, &lgfx::fonts::Font7);
    if (showUnit) {
      spr.setTextDatum(ML_DATUM);
      spr.setTextColor(unitColor);
      spr.drawString("MHz", ux, uy + 10, &lgfx::fonts::FreeSansBold9pt7b);
    }
  } else {
    if (isSsb) {
      const int64_t tunedHz = static_cast<int64_t>(freq) * 1000LL + currentBFO;
      int64_t wholeKHz = tunedHz / 1000LL;
      int32_t fractionHz = static_cast<int32_t>(tunedHz % 1000LL);
      if (fractionHz < 0) {
        fractionHz += 1000;
        --wholeKHz;
      }
      char value[24];
      snprintf(value, sizeof(value), "%3.3lu",
        static_cast<unsigned long>(wholeKHz));
      spr.drawString(value, x, y, &lgfx::fonts::Font7);
      spr.setTextDatum(ML_DATUM);
      snprintf(value, sizeof(value), ".%3.3lu",
        static_cast<unsigned long>(fractionHz));
      spr.drawString(value, 4 + x, 17 + y, &lgfx::fonts::Font4);
    } else {
      spr.drawNumber(freq, x, y, &lgfx::fonts::Font7);
    }

    if (showUnit) {
      spr.setTextColor(unitColor);
      spr.setTextDatum(ML_DATUM);
      spr.drawString("kHz", ux, uy + 10, &lgfx::fonts::FreeSansBold9pt7b);
    }
  }

  if (li) {
    const uint16_t color = selectOn ? selectedHighlightColor : normalHighlightColor;
    if (selectOn) {
      spr.fillRoundRect(li->x + 1, li->y - 1, li->w - 2, 3, 1, color);
      spr.fillTriangle(li->x, li->y, li->x + 2, li->y - 2,
                       li->x + 2, li->y - 2, color);
      spr.fillTriangle(li->x + li->w - 1, li->y,
                       li->x + li->w - 3, li->y - 2,
                       li->x + li->w - 3, li->y + 2, color);
    } else {
      spr.fillRoundRect(li->x, li->y - 1, li->w, 3, 1, color);
    }
  }
}

template <typename Canvas>
void draw(Canvas& canvas, const RadioSnapshot& state,
          int x, int y, int unitX, int unitY, uint8_t highlight,
          uint16_t frequencyColor, uint16_t normalHighlightColor,
          uint16_t selectedHighlightColor,
          uint16_t normalUnitColor = 0x07E0,
          bool showUnit = true) {
  draw(canvas, state.frequencyKHz, state.modulation, x, y, unitX, unitY,
       highlight, state.bfoOffsetHz, frequencyColor, normalHighlightColor,
       selectedHighlightColor, normalUnitColor, showUnit);
}

} // namespace LegacyFrequencyRenderer
