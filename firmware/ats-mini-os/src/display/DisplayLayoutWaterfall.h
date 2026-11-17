#pragma once

#include <Arduino.h>
#include "../radio/RadioSnapshot.h"
#include "../audio/AudioWaterfallService.h"
#include "DisplayStatusBar.h"

namespace LiveWaterfallLayout {
constexpr uint16_t BG = 0x0008;
constexpr uint16_t BLUE = 0x0359;
constexpr uint16_t FRAME = 0x047D;
constexpr uint16_t TEXT = 0xFFFF;
constexpr uint16_t GREEN = 0x07E0;
constexpr uint16_t CYAN = 0x07FF;
constexpr uint16_t YELLOW = 0xFFE0;
constexpr uint16_t RED = 0xF800;
constexpr uint16_t MUTED = 0x7BEF;

inline uint16_t colorFor(uint8_t value) {
  return value < 132 ? 0x0008 :
         value < 165 ? 0x001F :
         value < 195 ? CYAN :
         value < 225 ? GREEN :
         value < 245 ? YELLOW : TEXT;
}

template <typename Canvas>
void draw(Canvas& c, const RadioSnapshot& s, bool radioReady, bool wifi,
          AudioWaterfallService* waterfall, bool fm) {
  constexpr int left = 8, width = 304, top = 43, height = 82;
  constexpr uint16_t bins = 300, rowsMax = 80;
  static uint8_t pixels[rowsMax * bins];

  c.fillScreen(BG);
  drawNetworkBatteryStatus(c, wifi);
  c.setTextDatum(MC_DATUM);
  c.setTextColor(GREEN, BG);
  c.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  c.drawString(fm ? "UKW / FM LIVE WATERFALL" : "LW / MW / KW LIVE WATERFALL", 160, 4);

  char frequency[28];
  if (fm) {
    snprintf(frequency, sizeof(frequency), "%.2f MHz", s.frequencyKHz / 1000.0f);
  } else if (s.modulation == RadioModulation::USB || s.modulation == RadioModulation::LSB) {
    snprintf(frequency, sizeof(frequency), "%lu.%03lu kHz",
      static_cast<unsigned long>(s.frequencyKHz),
      static_cast<unsigned long>(static_cast<uint32_t>(abs(s.bfoOffsetHz)) % 1000));
  } else {
    snprintf(frequency, sizeof(frequency), "%lu kHz", static_cast<unsigned long>(s.frequencyKHz));
  }
  c.setFont(&lgfx::fonts::Font7);
  c.setTextColor(GREEN, BG);
  c.setTextSize(0.72f);
  c.drawString(frequency, 160, 21);
  c.setTextSize(1.0f);

  c.drawRect(left, top, width, height, FRAME);
  c.fillRect(left + 1, top + 1, width - 2, height - 2, 0x0000);
  uint16_t rows = 0, outBins = 0;
  const bool haveData = waterfall && waterfall->active() &&
    waterfall->copyWaterfall(pixels, rowsMax, bins, rows, outBins);
  if (haveData && outBins) {
    const uint16_t yOffset = static_cast<uint16_t>(rowsMax - rows);
    for (uint16_t y = 0; y < rows; ++y) {
      for (uint16_t x = 0; x < outBins; ++x) {
        c.drawPixel(left + 2 + x, top + 1 + yOffset + y,
          colorFor(pixels[static_cast<size_t>(y) * outBins + x]));
      }
    }
  } else {
    c.setTextDatum(MC_DATUM);
    c.setFont(&lgfx::fonts::Font0);
    c.setTextColor(YELLOW, 0x0000);
    c.drawString("AUDIO WATERFALL NOT AVAILABLE", 160, top + 34);
  }

  c.setTextDatum(TL_DATUM);
  c.setFont(&lgfx::fonts::Font0);
  c.setTextColor(MUTED, BG);
  c.drawString("100 Hz", left, 128);
  c.drawRightString("1900 Hz", left + width, 128);

  const char* mode = fm ? (s.stereo ? "FM STEREO" : "FM MONO") :
    (s.modulation == RadioModulation::AM ? "AM" :
     s.modulation == RadioModulation::USB ? "USB" : "LSB");
  c.setTextColor(CYAN, BG);
  c.drawString(mode, 10, 145);
  if (s.rssi >= 0) {
    char signal[20];
    snprintf(signal, sizeof(signal), "RSSI %d", s.rssi);
    c.setTextColor(GREEN, BG);
    c.drawRightString(signal, 310, 145);
  }
  c.setTextDatum(MC_DATUM);
  c.setTextColor(TEXT, BG);
  c.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  if (fm) {
    const char* station = s.stationName[0] ? s.stationName : (radioReady ? "NO RDS" : "RADIO OFFLINE");
    c.drawString(station, 160, 158);
  } else {
    c.drawString(radioReady ? "LIVE AUDIO SPECTRUM" : "RADIO OFFLINE", 160, 158);
  }
}
} // namespace LiveWaterfallLayout
