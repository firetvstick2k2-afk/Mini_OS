#pragma once

#include <Arduino.h>
#include "../radio/RadioSnapshot.h"
#include "../audio/AudioWaterfallService.h"
#include "DisplayStatusBar.h"

namespace LiveWaterfallLayout {
constexpr uint16_t BG=0x0000, WHITE=0xFFFF, GREEN=0x07E0, CYAN=0x07FF;
constexpr uint16_t YELLOW=0xFFE0, RED=0xF800, BLUE=0x001F, GREY=0x7BEF;

template <typename Canvas>
void draw(Canvas& d, const RadioSnapshot& s, bool ready, bool wifi,
          AudioWaterfallService* waterfall, bool fm) {
  d.fillScreen(BG);
  drawNetworkBatteryStatus(d, wifi, 8);

  // Keep the established ATS Mini Waterfall layout geometry: selector tiles
  // on the left, centered frequency, station row and the original 280x26 panel.
  const char* band = fm ? "VHF" : (s.bandName[0] ? s.bandName : "ALL");
  const char* mode = fm ? "FM" :
    (s.modulation == RadioModulation::AM ? "AM" :
     s.modulation == RadioModulation::USB ? "USB" : "LSB");
  const char* unit = fm ? "MHz" : "kHz";

  d.setFont(&lgfx::fonts::Font0);
  d.setTextDatum(MC_DATUM);
  d.setTextColor(CYAN, BG);
  d.drawRect(8,24,42,18,CYAN);
  d.drawString(band,29,33);
  d.drawRect(8,46,42,18,CYAN);
  d.drawString(mode,29,55);
  d.drawRect(8,68,42,18,CYAN);
  d.drawString(unit,29,77);

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
  d.setTextColor(WHITE,BG);
  d.setTextDatum(MC_DATUM);
  d.setFont(&lgfx::fonts::Font7);
  d.setTextSize(fm ? 0.86f : 0.78f);
  d.drawString(frequency,180,55);
  d.setTextSize(1.0f);
  d.setTextDatum(TL_DATUM);

  const char* station=s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE");
  d.setTextDatum(TC_DATUM);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.setTextColor(GREEN,BG);
  if(station && *station) d.drawString(station,160,91);

  constexpr int wfX=20, wfY=132, wfW=280, wfH=26;
  constexpr uint16_t rowsMax=23, bins=278;
  static uint8_t waterfallPixels[rowsMax*bins];
  d.drawRect(wfX,wfY,wfW,wfH,WHITE);
  d.drawFastHLine(wfX+1,wfY+1,wfW-2,WHITE);
  uint16_t rows=0,outBins=0;
  const bool haveData=waterfall && waterfall->active() &&
    waterfall->copyWaterfall(waterfallPixels,rowsMax,bins,rows,outBins);
  if(haveData && outBins) {
    const uint16_t top=static_cast<uint16_t>(rowsMax-rows);
    for(uint16_t y=0;y<rows;++y) {
      for(uint16_t x=0;x<outBins;++x) {
        const uint8_t v=waterfallPixels[static_cast<size_t>(y)*outBins+x];
        const uint16_t color=v<132?0x0008:v<165?BLUE:v<195?CYAN:v<225?GREEN:v<245?YELLOW:WHITE;
        d.drawPixel(wfX+1+x,wfY+2+top+y,color);
      }
    }
  } else {
    d.setTextDatum(MC_DATUM);
    d.setFont(&lgfx::fonts::Font0);
    d.setTextColor(YELLOW,BG);
    d.drawString("WAITING FOR AUDIO",160,wfY+8);
  }

  d.setTextDatum(TL_DATUM);
  d.setFont(&lgfx::fonts::Font0);
  d.setTextColor(GREY,BG);
  if(fm) {
    d.drawString("100 Hz",20,115);
    d.drawRightString("1900 Hz",300,115);
  } else {
    d.drawString("100 Hz",20,115);
    d.drawRightString("1900 Hz",300,115);
  }
  if(s.rssi>=0) {
    char signal[18];
    snprintf(signal,sizeof(signal),"RSSI %d",s.rssi);
    d.setTextColor(WHITE,BG);
    d.drawString(signal,8,101);
  }
  d.setTextDatum(TL_DATUM);
}
inline void drawFm(auto& d,const RadioSnapshot& s,bool ready,bool wifi,AudioWaterfallService* w) {
  draw(d,s,ready,wifi,w,true);
}
inline void drawAm(auto& d,const RadioSnapshot& s,bool ready,bool wifi,AudioWaterfallService* w) {
  draw(d,s,ready,wifi,w,false);
}
} // namespace LiveWaterfallLayout
