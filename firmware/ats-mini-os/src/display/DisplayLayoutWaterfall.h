#pragma once

#include <Arduino.h>
#include <stdio.h>
#include "../radio/RadioSnapshot.h"
#include "../audio/AudioWaterfallService.h"
#include "DisplayStatusBar.h"

namespace LiveWaterfallLayout {
constexpr uint16_t BG=0x0000, WHITE=0xFFFF, BLUE=0x001F, DEEP_BLUE=0x0008;
constexpr uint16_t CYAN=0x07FF, PALE_BLUE=0xBFFF, GREEN=0x07E0;

template <typename Canvas>
void draw(Canvas& d, const RadioSnapshot& s, bool ready, bool wifi,
          AudioWaterfallService* waterfall, bool fm) {
  d.fillScreen(BG);
  drawNetworkBatteryStatus(d, wifi, 8);

  // Compact radio header; reserve almost the entire lower display for the
  // live FFT waterfall. No RSSI or frequency-axis labels in this view.
  const char* band = fm ? "FM" : (s.bandName[0] ? s.bandName : "ALL");
  const char* mode = fm ? "FM" :
    (s.modulation == RadioModulation::AM ? "AM" :
     s.modulation == RadioModulation::USB ? "USB" : "LSB");
  d.setFont(&lgfx::fonts::Font0);
  d.setTextDatum(TL_DATUM);
  d.setTextColor(CYAN, BG);
  d.drawString(band, 5, 25);
  d.drawString(mode, 5, 37);

  char frequency[24];
  if (fm) {
    snprintf(frequency,sizeof(frequency),"%.2f",s.frequencyKHz/1000.0f);
  } else if (s.modulation == RadioModulation::USB || s.modulation == RadioModulation::LSB) {
    const int64_t tunedHz=static_cast<int64_t>(s.frequencyKHz)*1000LL+s.bfoOffsetHz;
    const uint32_t safeHz=static_cast<uint32_t>(tunedHz<0?0:tunedHz);
    snprintf(frequency,sizeof(frequency),"%lu.%03lu",
      static_cast<unsigned long>(safeHz/1000UL),
      static_cast<unsigned long>(safeHz%1000UL));
  } else {
    snprintf(frequency,sizeof(frequency),"%lu",static_cast<unsigned long>(s.frequencyKHz));
  }
  d.setFont(&lgfx::fonts::Font7);
  d.setTextDatum(MC_DATUM);
  d.setTextColor(WHITE,BG);
  d.setTextSize(fm ? 0.78f : 0.70f);
  d.drawString(frequency,174,34);
  d.setTextSize(1.0f);

  const char* station=s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE");
  if(station && *station) {
    d.setFont(&lgfx::fonts::Font0);
    d.setTextDatum(MR_DATUM);
    d.setTextColor(GREEN,BG);
    d.drawString(station,314,45);
  }

  // Nearly edge-to-edge spectrum: real FFT bins from the audio capture,
  // with row-local contrast so narrow carriers stand out from the noise floor.
  constexpr int wfX=3, wfY=51, wfW=314, wfH=119;
  constexpr uint16_t rowsMax=64, bins=312;
  static uint8_t waterfallPixels[rowsMax*bins];
  d.fillRect(wfX,wfY,wfW,wfH,DEEP_BLUE);
  uint16_t rows=0,outBins=0;
  const bool haveData=waterfall && waterfall->active() &&
    waterfall->copyWaterfall(waterfallPixels,rowsMax,bins,rows,outBins);
  if(haveData && outBins) {
    const uint16_t visibleRows = rows < wfH ? rows : wfH;
    const uint16_t firstRow = rows - visibleRows;
    for(uint16_t y=0;y<visibleRows;++y) {
      const size_t rowBase=static_cast<size_t>(firstRow+y)*outBins;
      uint8_t low=255, high=0;
      for(uint16_t x=0;x<outBins;++x) {
        const uint8_t v=waterfallPixels[rowBase+x];
        if(v<low) low=v;
        if(v>high) high=v;
      }
      const uint8_t span=high>low?static_cast<uint8_t>(high-low):1;
      const uint8_t floorLevel=static_cast<uint8_t>(low+(static_cast<uint16_t>(span)*18U)/100U);
      const uint8_t fullLevel=static_cast<uint8_t>(low+(static_cast<uint16_t>(span)*78U)/100U);
      for(uint16_t x=0;x<outBins;++x) {
        const uint8_t v=waterfallPixels[rowBase+x];
        uint16_t color=DEEP_BLUE;
        if(v>floorLevel) {
          const uint16_t level=fullLevel>floorLevel
            ? static_cast<uint16_t>(min(255U,(static_cast<uint16_t>(v-floorLevel)*255U)/(fullLevel-floorLevel)))
            : 255U;
          if(level<55) color=BLUE;
          else if(level<125) color=CYAN;
          else if(level<205) color=PALE_BLUE;
          else color=WHITE;
        }
        d.drawPixel(wfX+x,wfY+(wfH-visibleRows)+y,color);
      }
    }
  } else {
    d.setFont(&lgfx::fonts::Font0);
    d.setTextDatum(MC_DATUM);
    d.setTextColor(CYAN,BG);
    d.drawString("WAITING FOR AUDIO",160,wfY+wfH/2);
  }
  d.setTextDatum(TL_DATUM);
}
template <typename Canvas>
inline void drawFm(Canvas& d,const RadioSnapshot& s,bool ready,bool wifi,AudioWaterfallService* w) {
  draw(d,s,ready,wifi,w,true);
}
template <typename Canvas>
inline void drawAm(Canvas& d,const RadioSnapshot& s,bool ready,bool wifi,AudioWaterfallService* w) {
  draw(d,s,ready,wifi,w,false);
}
} // namespace LiveWaterfallLayout
