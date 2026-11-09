#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "../radio/RadioSnapshot.h"

// Optional, standalone port of ATS Mini DEV's Draw.cpp::drawFrequency().
// This helper deliberately does not replace any Mini OS layout renderer.
// A layout opts in by including this header and calling
// LegacyFrequencyRenderer::draw(...), choosing its own coordinates/colors.
namespace LegacyFrequencyRenderer {

struct Line { int x; int y; int w; };

template <typename Canvas>
void draw(Canvas& canvas,
          uint32_t frequencyKHz,
          RadioModulation modulation,
          int x, int y, int unitX, int unitY,
          uint8_t highlight = 0,
          int16_t bfoHz = 0,
          uint16_t frequencyColor = 0xFFFF,
          uint16_t unitColorNormal = 0x07E0,
          uint16_t highlightColor = 0x07FF) {
  static uint32_t lastFrequency = 0;
  static uint32_t lastTuneTime = 0;
  if (frequencyKHz != lastFrequency) {
    lastFrequency = frequencyKHz;
    lastTuneTime = millis();
  }
  const uint16_t unitColor = (millis() - lastTuneTime < 800)
    ? 0xFFE0 : unitColorNormal;

  const Line fmDigits[] = {
    {x-30, y+28, 27}, {x-46, y+28, 43},
    {x-62, y+28, 27}, {x-84, y+28, 49},
    {x-106, y+28, 27}, {x-118, y+28, 43},
    {x-158, y+28, 27}, {x-174, y+28, 43},
    {x-154, y+28, 11}
  };
  const Line amSsbDigits[] = {
    {x+40,y+28,12},{x+33,y+28,19},
    {x+26,y+28,12},{x+19,y+28,19},
    {x+12,y+28,12},{x+1,y+28,23},
    {x-30,y+28,27},{x-46,y+28,43},
    {x-62,y+28,27},{x-78,y+28,43},
    {x-94,y+28,27},{x-110,y+28,43},
    {x-126,y+28,27},{x-142,y+28,43},
    {x-158,y+28,27}
  };

  const bool selected = (highlight & 0x80) != 0;
  const uint8_t digit = highlight & 0x7F;
  const bool isFm = modulation == RadioModulation::FM;
  const bool isSsb = modulation == RadioModulation::USB ||
                     modulation == RadioModulation::LSB;
  const Line* activeLine = nullptr;
  const size_t lineCount = isFm
    ? sizeof(fmDigits)/sizeof(fmDigits[0])
    : sizeof(amSsbDigits)/sizeof(amSsbDigits[0]);
  if (digit < lineCount)
    activeLine = isFm ? &fmDigits[digit] : &amSsbDigits[digit];

  canvas.setTextDatum(MR_DATUM);
  canvas.setTextColor(frequencyColor);
  canvas.setFont(&lgfx::fonts::Font7);
  if (isFm) {
    canvas.drawFloat(frequencyKHz / 100.0f, 2, x, y);
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextColor(unitColor);
    canvas.setFont(&lgfx::fonts::FreeSansBold9pt7b);
    canvas.drawString("MHz", unitX, unitY + 10);
  } else {
    if (isSsb) {
      const int64_t tunedHz = static_cast<int64_t>(frequencyKHz) * 1000LL + bfoHz;
      const uint32_t wholeKHz = static_cast<uint32_t>(tunedHz / 1000LL);
      const uint16_t fractionHz = static_cast<uint16_t>(
        (tunedHz % 1000LL + 1000LL) % 1000LL);
      char whole[20];
      char fraction[8];
      snprintf(whole, sizeof(whole), "%lu",
               static_cast<unsigned long>(wholeKHz));
      snprintf(fraction, sizeof(fraction), ".%03u",
               static_cast<unsigned>(fractionHz));
      canvas.drawString(whole, x, y);
      canvas.setTextDatum(ML_DATUM);
      canvas.setFont(&lgfx::fonts::Font4);
      canvas.drawString(fraction, x + 4, y + 17);
    } else {
      canvas.drawNumber(frequencyKHz, x, y);
    }
    canvas.setTextDatum(ML_DATUM);
    canvas.setTextColor(unitColor);
    canvas.setFont(&lgfx::fonts::FreeSansBold9pt7b);
    canvas.drawString("kHz", unitX, unitY + 10);
  }

  if (activeLine) {
    const uint16_t color = selected ? highlightColor : highlightColor;
    if (selected) {
      canvas.fillRoundRect(activeLine->x + 1, activeLine->y - 1,
                           activeLine->w - 2, 3, 1, color);
      canvas.fillTriangle(activeLine->x, activeLine->y,
                          activeLine->x + 2, activeLine->y - 2,
                          activeLine->x + 2, activeLine->y - 2, color);
      canvas.fillTriangle(activeLine->x + activeLine->w - 1, activeLine->y,
                          activeLine->x + activeLine->w - 3, activeLine->y - 2,
                          activeLine->x + activeLine->w - 3, activeLine->y + 2,
                          color);
    } else {
      canvas.fillRoundRect(activeLine->x, activeLine->y - 1,
                           activeLine->w, 3, 1, color);
    }
  }
}

template <typename Canvas>
void draw(Canvas& canvas, const RadioSnapshot& state,
          int x, int y, int unitX, int unitY,
          uint8_t highlight = 0, int16_t bfoHz = 0,
          uint16_t frequencyColor = 0xFFFF,
          uint16_t unitColorNormal = 0x07E0,
          uint16_t highlightColor = 0x07FF) {
  draw(canvas, state.frequencyKHz, state.modulation,
       x, y, unitX, unitY, highlight, bfoHz,
       frequencyColor, unitColorNormal, highlightColor);
}

} // namespace LegacyFrequencyRenderer
