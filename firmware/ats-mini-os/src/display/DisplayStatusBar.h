#pragma once
#include <Arduino.h>

// Shared ATS Mini OS WLAN and battery indicator.
// The battery divider calibration matches the existing HJB/DAB renderers.
template <typename Canvas>
void drawNetworkBatteryStatus(Canvas& d, bool wifiConnected, int leftX = 8) {
  constexpr uint16_t GREEN = 0x07E0;
  constexpr uint16_t CYAN = 0x07FF;
  constexpr uint16_t MUTED = 0x7BEF;
  constexpr uint16_t YELLOW = 0xFFE0;
  const int w = d.width();
  d.setTextDatum(TL_DATUM);
  d.setFont(&lgfx::fonts::Font0);
  d.setTextColor(wifiConnected ? CYAN : MUTED, 0x0000);
  d.drawString(wifiConnected ? "WLAN+" : "WLAN-", leftX, 5);

  uint32_t total = 0;
  for (uint8_t i = 0; i < 10; ++i) total += analogRead(4);
  const float volts = (static_cast<float>(total) / 10.0f) * 1.702f / 1000.0f;
  const int percent = constrain(static_cast<int>((volts - 3.30f) * 100.0f / 0.90f + 0.5f), 0, 100);
  char label[8];
  snprintf(label, sizeof(label), "%d%%", percent);
  d.setTextDatum(TR_DATUM);
  d.setTextColor(percent <= 15 ? YELLOW : GREEN, 0x0000);
  d.drawString(label, w - 8, 5);
  d.setTextDatum(TL_DATUM);
}
