#pragma once
#include <Arduino.h>
#include <stdio.h>
#include "../radio/RadioSnapshot.h"
#include "../radio/FmLivePanoramaService.h"
#include "DisplayStatusBar.h"

namespace FmPanoramaLayout {
constexpr uint16_t BG=0x0000, DEEP_BLUE=0x0008, BLUE=0x001F;
constexpr uint16_t CYAN=0x07FF, GREEN=0x07E0, WHITE=0xFFFF;

inline uint16_t signalColor(uint8_t value) {
  if (value < 18) return DEEP_BLUE;
  if (value < 38) return BLUE;
  if (value < 58) return CYAN;
  if (value < 78) return 0xBFE0;
  return WHITE;
}

template <typename Canvas>
void draw(Canvas& d, const RadioSnapshot& s, bool wifi,
          const FmLivePanoramaService* panorama) {
  d.fillScreen(BG);
  drawNetworkBatteryStatus(d, wifi, 8);

  d.setFont(&lgfx::fonts::Font0);
  d.setTextDatum(TL_DATUM);
  d.setTextColor(GREEN, BG);
  if (s.stationName[0]) {
    d.setClipRect(5, 25, 150, 22);
    d.drawString(s.stationName, 5, 29);
    d.clearClipRect();
  } else {
    d.setTextColor(CYAN, BG);
    d.drawString("FM RF PANORAMA", 5, 29);
  }

  char frequency[20];
  snprintf(frequency, sizeof(frequency), "%.2f MHz", s.frequencyKHz / 1000.0f);
  d.setFont(&lgfx::fonts::Font7);
  d.setTextDatum(MR_DATUM);
  d.setTextColor(WHITE, BG);
  d.setTextSize(0.55f);
  d.drawString(frequency, 314, 27);
  d.setTextSize(1.0f);

  constexpr int x0=3, y0=54, width=314, height=112;
  constexpr int columns=FmPanoramaService::kPointCount;
  constexpr int cellW=width/columns;
  constexpr int cellH=4;
  d.drawRect(x0-1, y0-1, width+2, height+2, BLUE);
  d.fillRect(x0, y0, width, height, DEEP_BLUE);

  const uint8_t rows = panorama ? panorama->rowCount() : 0;
  for (uint8_t r=0; r<rows; ++r) {
    const auto* points = panorama->row(r);
    if (!points) continue;
    const int y = y0 + height - cellH - (static_cast<int>(rows-1-r)*cellH);
    for (int col=0; col<columns; ++col) {
      const uint16_t x = x0 + col*cellW;
      const uint16_t color = signalColor(points[col].rssi);
      d.fillRect(x, y, cellW-1, cellH-1, color);
    }
  }

  d.setFont(&lgfx::fonts::Font0);
  d.setTextDatum(TL_DATUM);
  d.setTextColor(CYAN, BG);
  d.drawString("88 MHz", 4, 39);
  d.setTextDatum(MR_DATUM);
  d.drawString("108 MHz", 316, 39);
  if (!rows) {
    d.setTextDatum(MC_DATUM);
    d.setTextColor(CYAN, BG);
    d.drawString("FM SCAN STARTET...", 160, 108);
  }
  d.setTextDatum(TL_DATUM);
}
} // namespace FmPanoramaLayout
