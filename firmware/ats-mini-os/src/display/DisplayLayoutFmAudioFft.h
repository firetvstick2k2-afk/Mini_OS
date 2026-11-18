#pragma once
#include <Arduino.h>
#include <stdio.h>
#include "../radio/RadioSnapshot.h"

namespace FmAudioFftLayout {
constexpr uint16_t BG=0x0000, DEEP_BLUE=0x0008, BLUE=0x001F;
constexpr uint16_t CYAN=0x07FF, WHITE=0xFFFF;

inline uint16_t blendColor(uint16_t a, uint16_t b, uint8_t amount) {
  const uint16_t ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const uint16_t br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const uint16_t r = ar + ((static_cast<int>(br) - ar) * amount) / 255;
  const uint16_t g = ag + ((static_cast<int>(bg) - ag) * amount) / 255;
  const uint16_t bl = ab + ((static_cast<int>(bb) - ab) * amount) / 255;
  return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

// Higher contrast palette: keep blues deep and reserve bright tones for strong peaks.
inline uint16_t color(uint8_t value) {
  static constexpr uint8_t stops[] = {0, 42, 82, 118, 150, 205};
  static constexpr uint16_t colors[] = {DEEP_BLUE, 0x000F, 0x0018, 0x03BF, 0x07FF, WHITE};
  for (uint8_t i = 0; i < 5; ++i) {
    if (value <= stops[i + 1]) {
      const uint8_t span = stops[i + 1] - stops[i];
      const uint8_t amount = span ? static_cast<uint8_t>((static_cast<uint16_t>(value - stops[i]) * 255) / span) : 255;
      return blendColor(colors[i], colors[i + 1], amount);
    }
  }
  return WHITE;
}

template <typename Canvas>
void drawBatteryPercent(Canvas& d) {
  constexpr uint16_t GREEN = 0x07E0;
  constexpr uint16_t YELLOW = 0xFFE0;
  uint32_t total = 0;
  for (uint8_t i = 0; i < 10; ++i) total += analogRead(4);
  const float volts = (static_cast<float>(total) / 10.0f) * 1.702f / 1000.0f;
  const int percent = constrain(static_cast<int>((volts - 3.30f) * 100.0f / 0.90f + 0.5f), 0, 100);
  char label[8];
  snprintf(label, sizeof(label), "%d%%", percent);
  d.setTextDatum(TR_DATUM);
  d.setFont(&lgfx::fonts::Font0);
  d.setTextColor(percent <= 15 ? YELLOW : GREEN, BG);
  d.drawString(label, 315, 5);
}

template <typename Canvas>
void draw(Canvas& d, const RadioSnapshot& s, const uint8_t* waterfall, bool available) {
  d.fillScreen(BG);

  // Station identity and battery stay in the top row.
  d.setTextDatum(TL_DATUM);
  d.setFont(&lgfx::fonts::Font2);
  d.setTextColor(s.stationName[0] ? 0x07E0 : CYAN, BG);
  d.setClipRect(5, 3, 230, 22);
  d.drawString(s.stationName[0] ? s.stationName : "FM / RDS", 5, 5);
  d.clearClipRect();
  drawBatteryPercent(d);

  // Compact receiver status, followed by a larger frequency immediately above FFT.
  d.setTextDatum(TL_DATUM);
  d.setFont(&lgfx::fonts::Font0);
  d.setTextColor(CYAN, BG);
  char status[40];
  snprintf(status, sizeof(status), "%s  RSSI %d  SNR %d", s.stereo ? "FM STEREO" : "FM MONO", s.rssi, s.snr);
  d.drawString(status, 5, 25);

  char frequency[20];
  snprintf(frequency, sizeof(frequency), "%.2f MHz", s.frequencyKHz / 1000.0f);
  d.setTextDatum(TR_DATUM);
  d.setTextColor(WHITE, BG);
  d.setFont(&lgfx::fonts::Font4);
  d.drawString(frequency, 315, 39);

  constexpr int x0=3, y0=62, width=314, height=102;
  d.drawRect(1, 60, 318, 106, BLUE);
  d.fillRect(x0, y0, width, height, BG);
  if (available && waterfall) {
    // Preserve the original measured pixels: no spatial averaging or blur.
    for (uint8_t row = 0; row < 64; ++row) {
      const int y = y0 + static_cast<int>(row) * height / 64;
      const int rowHeight = (static_cast<int>(row + 1) * height / 64) -
                            (static_cast<int>(row) * height / 64);
      for (uint8_t bin = 0; bin < 64; ++bin) {
        const int x = x0 + static_cast<int>(bin) * width / 64;
        const int columnWidth = (static_cast<int>(bin + 1) * width / 64) -
                                (static_cast<int>(bin) * width / 64);
        const uint8_t value = waterfall[static_cast<size_t>(row) * 64 + bin];
        d.fillRect(x, y, columnWidth, rowHeight, color(value));
      }
    }
  } else {
    d.setTextDatum(MC_DATUM);
    d.setTextColor(CYAN, BG);
    d.setFont(&lgfx::fonts::Font2);
    d.drawString("AUDIO FFT WARTET...", 160, 113);
  }
  d.setTextDatum(TL_DATUM);
}
} // namespace FmAudioFftLayout
