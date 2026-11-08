#pragma once
#include <Arduino.h>
#include "../radio/RadioSnapshot.h"
#include <string.h>

// ATS Mini DEV LW/MW/KW layouts adapted to the OS RadioSnapshot contract.
// Each renderer is isolated from the FM/DAB and TEF6686 renderers.
namespace SWLayouts {
constexpr uint16_t BG=0x0000, WHITE=0xFFFF, GREEN=0x07E0, CYAN=0x07FF;
constexpr uint16_t YELLOW=0xFFE0, RED=0xF800, GREY=0x7BEF, BLUE=0x001F;

template<class C> void header(C& d,const RadioSnapshot& s,bool wifi) {
  d.setTextDatum(TL_DATUM); d.setTextColor(GREEN,BG);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b); d.drawString(s.bandName,8,3);
  d.setFont(&lgfx::fonts::Font0); d.setTextColor(wifi?CYAN:GREY,BG);
  d.drawRightString(wifi?"WLAN+":"WLAN-",312,5);
}
template<class C> void frequency(C& d,const RadioSnapshot& s,int x,int y,
                         float scale=0.72f,int unitX=250,int unitY=78) {
  char f[24];
  if(s.modulation==RadioModulation::USB || s.modulation==RadioModulation::LSB)
    snprintf(f,sizeof(f),"%lu.000",static_cast<unsigned long>(s.frequencyKHz));
  else snprintf(f,sizeof(f),"%lu",static_cast<unsigned long>(s.frequencyKHz));
  d.setFont(&lgfx::fonts::Font7); d.setTextColor(GREEN,BG); d.setTextDatum(TL_DATUM);
  d.setTextSize(scale); d.drawString(f,x,y); d.setTextSize(1.0f);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b); d.setTextColor(WHITE,BG);
  d.drawString("kHz",unitX,unitY);
}
template<class C> void meter(C& d,const RadioSnapshot& s,int x,int y,int width) {
  const int v=constrain(static_cast<int>(s.rssi),0,60);
  const int fill=map(v,0,60,0,width);
  d.drawRect(x,y,width,13,WHITE); d.fillRect(x+1,y+1,width-2,11,0x2104);
  const int g=width*65/100, a=width*82/100;
  if(fill>0)d.fillRect(x+1,y+1,min(fill,g),11,GREEN);
  if(fill>g)d.fillRect(x+1+g,y+1,min(fill-g,a-g),11,YELLOW);
  if(fill>a)d.fillRect(x+1+a,y+1,fill-a,11,RED);
}
inline const char* bandplan(uint32_t f) {
  if((f>=1839&&f<=1841)||(f>=3572&&f<=3574)||(f>=5356&&f<=5358)||
     (f>=7073&&f<=7075)||(f>=10135&&f<=10137)||(f>=14070&&f<=14077)||
     (f>=18099&&f<=18101)||(f>=21073&&f<=21075)||(f>=24914&&f<=24916)||
     (f>=28070&&f<=28080)) return "FT8 - USB";
  if((f>=1800&&f<=1837)||(f>=3500&&f<=3560)||(f>=7000&&f<=7040)||
     (f>=10100&&f<=10130)||(f>=14000&&f<=14069)||(f>=18068&&f<=18095)||
     (f>=21000&&f<=21069)||(f>=24890&&f<=24910)||(f>=28000&&f<=28069)) return "CW - MORSE";
  if((f>=1843&&f<=2000)||(f>=3600&&f<=3800)||(f>=7060&&f<=7200)) return "QSO - LSB";
  if((f>=5354&&f<=5366)||(f>=14100&&f<=14350)||(f>=18110&&f<=18168)||
     (f>=21150&&f<=21450)||(f>=24931&&f<=24990)||(f>=28300&&f<=29100)) return "QSO - USB";
  if((f>=3950&&f<=4000)||(f>=5900&&f<=6200)||(f>=7200&&f<=7450)||
     (f>=9400&&f<=9900)||(f>=11600&&f<=12100)||(f>=13570&&f<=13870)||
     (f>=15100&&f<=15800)||(f>=17480&&f<=17900)||(f>=21450&&f<=21850)) return "SW RADIO AM";
  if(f>=26565&&f<=27405)return "CB - AM / FM / SSB";
  return "MIXED";
}

template<class C> void drawDefault(C& d,const RadioSnapshot& s,bool ready,bool wifi) {
  d.fillScreen(BG); header(d,s,wifi);
  d.setTextColor(WHITE,BG); d.setFont(&lgfx::fonts::Font7); d.setTextSize(0.85f);
  d.drawString(s.modulation==RadioModulation::AM?"AM":s.modulation==RadioModulation::USB?"USB":"LSB",8,24);
  d.setTextSize(1.0f); frequency(d,s,8,48,0.78f);
  d.setTextColor(GREEN,BG); d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.drawString(s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE"),8,92);
  meter(d,s,45,119,230);
  d.setFont(&lgfx::fonts::Font0); d.setTextColor(WHITE,BG);
  d.drawString(s.radioText,8,143);
}
template<class C> void drawClean(C& d,const RadioSnapshot& s,bool ready,bool wifi) {
  d.fillScreen(BG); header(d,s,wifi);
  d.fillRoundRect(9,22,50,19,4,GREEN); d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.setTextDatum(MC_DATUM); d.setTextColor(BG,GREEN);
  const char* mode=s.modulation==RadioModulation::AM?"AM":s.modulation==RadioModulation::USB?"USB":"LSB";
  d.drawString(mode,34,31);
  d.setTextDatum(TL_DATUM); d.setTextColor(GREEN,BG);
  frequency(d,s,74,30,0.67f);
  d.drawRoundRect(18,101,284,39,5,GREEN);
  d.setTextColor(WHITE,BG); d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.setTextDatum(MC_DATUM); d.drawString(bandplan(s.frequencyKHz),160,112);
  d.setTextDatum(TL_DATUM); meter(d,s,50,151,220);
  if(!ready){d.setFont(&lgfx::fonts::Font0);d.setTextColor(YELLOW,BG);d.drawString("RADIO OFFLINE",8,82);}
}
template<class C> void drawNewUI(C& d,const RadioSnapshot& s,bool ready,bool wifi) {
  d.fillScreen(BG); header(d,s,wifi);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b); d.setTextColor(WHITE,BG);
  d.drawCenterString(s.modulation==RadioModulation::AM?"AM":s.modulation==RadioModulation::USB?"USB":"LSB",160,22);
  frequency(d,s,12,48,0.82f);
  d.setTextDatum(MC_DATUM); d.setTextColor(GREEN,BG);
  d.drawString(s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE"),160,94);
  const int x=12,y=132,w=296,parts=19;
  d.drawRoundRect(x,y,w,23,4,WHITE);
  const int active=constrain(map(max(0,static_cast<int>(s.rssi)),0,60,0,parts),0,parts);
  for(int i=0;i<parts;i++){
    const uint16_t col=i<6?GREEN:(i<13?YELLOW:RED);
    const int px=x+4+i*15;
    d.fillRect(px,y+5,11,13,i<active?col:0x2104);
  }
  d.setTextDatum(TL_DATUM); d.setFont(&lgfx::fonts::Font0);
}
template<class C> void drawSMeter(C& d,const RadioSnapshot& s,bool ready,bool wifi) {
  d.fillScreen(BG); header(d,s,wifi);
  frequency(d,s,8,28,0.72f);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b); d.setTextColor(WHITE,BG);
  d.drawString(s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE"),8,78);
  d.setFont(&lgfx::fonts::Font0);
  for(int i=0;i<=15;i++){
    const int x=12+i*19; d.drawFastVLine(x,111,5,GREY);
    if(i%2==0){char n[8];snprintf(n,sizeof(n),"%s%d",i>9?"+":"",i>9?(i-9)*10:i/2);d.drawCenterString(n,x,101);}
  }
  const int v=constrain(static_cast<int>(s.rssi),0,60);
  const int bars=map(v,0,60,0,49);
  for(int i=0;i<49;i++)d.fillRect(12+i*6,119,4,12,i<bars?(i<28?GREEN:YELLOW):0x2104);
  d.setTextColor(CYAN,BG); d.drawString("RSSI",12,140);
  char sn[16];snprintf(sn,sizeof(sn),"SNR %d",s.snr);d.drawString(sn,75,140);
  d.setTextColor(WHITE,BG);d.drawString(s.radioText,12,155);
}
template<class C> void drawDecoder(C& d,const RadioSnapshot& s,bool ready,bool wifi) {
  d.fillScreen(BG);
  header(d,s,wifi);
  frequency(d,s,8,26,0.70f);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.setTextColor(GREEN,BG);
  d.drawCenterString(s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE"),160,88);

  // Original ATS Mini DEV waterfall: 280 x 26 px frame, 278 x 23 px sprite.
  // The scrolling, RSSI mapping, three-color bands and +/-2 px artificial
  // jitter intentionally follow Layout-Decoder.cpp without visual redesign.
  constexpr int wfX=20, wfY=132, wfW=280, wfH=26;
  d.drawRect(wfX,wfY,wfW,wfH,WHITE);
  d.drawFastHLine(wfX+1,wfY+1,wfW-2,WHITE);

  constexpr int wfInnerW=wfW-2;
  constexpr int wfInnerH=wfH-3;
  static lgfx::LGFX_Sprite wfSpr(&d);
  static bool wfInitialized=false;

  if(!wfInitialized || wfSpr.width()!=wfInnerW || wfSpr.height()!=wfInnerH) {
    if(wfInitialized) wfSpr.deleteSprite();
    wfSpr.setPsram(true);
    wfSpr.setColorDepth(16);
    wfSpr.createSprite(wfInnerW,wfInnerH);
    wfSpr.fillScreen(BLUE);
    wfInitialized=true;
  }

  // One new signal row enters at the top; older rows move down exactly one px.
  wfSpr.scroll(0,1);
  wfSpr.fillRect(0,0,wfInnerW,1,BLUE);

  const uint8_t currentRssi=static_cast<uint8_t>(constrain(s.rssi,0,255));
  const int center=wfInnerW/2;
  const int maxRssi=(s.modulation==RadioModulation::FM)?35:45;
  constexpr int maxHalfWidth=55;
  const int baseWidth=map(currentRssi,0,maxRssi,4,maxHalfWidth);
  const int jitter=(rand()%5)-2;
  const int halfWidth=constrain(baseWidth+jitter,4,maxHalfWidth);

  // Green core.
  const int coreW=max(2,halfWidth);
  wfSpr.fillRect(center-coreW/2,0,coreW,1,GREEN);

  // Yellow transition bands.
  const int yellowW=max(2,halfWidth/3);
  wfSpr.fillRect(center-coreW/2-yellowW,0,yellowW,1,YELLOW);
  wfSpr.fillRect(center+coreW/2,0,yellowW,1,YELLOW);

  // Red outer edges.
  constexpr int redW=3;
  const int leftRedX=center-coreW/2-yellowW-redW;
  const int rightRedX=center+coreW/2+yellowW;
  if(leftRedX>=0) wfSpr.fillRect(leftRedX,0,redW,1,RED);
  if(rightRedX+redW<=wfInnerW) wfSpr.fillRect(rightRedX,0,redW,1,RED);

  wfSpr.pushSprite(&d,wfX+1,wfY+2);
  d.setTextDatum(TL_DATUM);
  d.setFont(&lgfx::fonts::Font0);
  d.setTextColor(WHITE,BG);
}
template<class C> void drawTiny(C& d,const RadioSnapshot& s,bool ready,bool wifi) {
  constexpr uint16_t DIM=0x8410;
  d.fillScreen(BG);

  // TinyRadio reference geometry: all coordinates target the physical 320x170 panel.
  // The OS snapshot has no battery/Bluetooth/AGC/BW state, so unavailable
  // controls are intentionally not fabricated.
  const char* bands[]={"LW","MW","SW","VHF","BT","WEB"};
  const int bandX[]={2,37,72,107,142,177};
  for(int i=0;i<6;i++) {
    bool selected=false;
    if(i==0) selected=strncmp(s.bandName,"LW",2)==0;
    else if(i==1) selected=strncmp(s.bandName,"MW",2)==0;
    else if(i==2) selected=(strncmp(s.bandName,"SW",2)==0 || strncmp(s.bandName,"KW",2)==0);
    else if(i==3) selected=strncmp(s.bandName,"VHF",3)==0;
    const uint16_t color=selected?CYAN:DIM;
    d.drawRect(bandX[i],3,32,17,color);
    d.setTextColor(color,BG);
    d.setTextDatum(MC_DATUM);
    d.setFont(&lgfx::fonts::Font0);
    d.drawString(bands[i],bandX[i]+16,11);
  }
  d.setTextDatum(TR_DATUM);
  d.setTextColor(wifi?CYAN:DIM,BG);
  d.drawString(wifi?"WLAN+":"WLAN-",312,6);

  // Keep frequency and unit together, using the original TinyRadio formatting.
  char freq[32];
  if(s.modulation==RadioModulation::USB || s.modulation==RadioModulation::LSB) {
    snprintf(freq,sizeof(freq),"%lu.000 kHz",
      static_cast<unsigned long>(s.frequencyKHz));
  } else {
    snprintf(freq,sizeof(freq),"%lu kHz",
      static_cast<unsigned long>(s.frequencyKHz));
  }
  d.setTextColor(YELLOW,BG);
  d.setTextDatum(TL_DATUM);
  d.setFont(&lgfx::fonts::Font7);
  d.setTextSize(0.82f);
  d.drawString(freq,8,36);
  d.setTextSize(1.0f);

  // Modulation selector row, with the current receiver mode highlighted.
  const char* modes[]={"FM","AM","LSB","USB"};
  const RadioModulation values[]={RadioModulation::FM,RadioModulation::AM,
                                  RadioModulation::LSB,RadioModulation::USB};
  const int modeX[]={3,43,83,123};
  for(int i=0;i<4;i++) {
    const bool active=s.modulation==values[i];
    const uint16_t color=active?CYAN:DIM;
    d.drawRect(modeX[i],66,36,21,color);
    d.setTextColor(color,BG);
    d.setTextDatum(MC_DATUM);
    d.setFont(&lgfx::fonts::Font0);
    d.drawString(modes[i],modeX[i]+18,76);
  }

  // Compact live receiver status; only values present in RadioSnapshot are shown.
  const int signal=constrain(static_cast<int>(s.rssi),0,80);
  for(int i=0;i<8;i++)
    d.fillRect(5+i*6,93,4,8,i<(signal>0?(signal>80?8:signal/10):0)?GREEN:DIM);
  d.setTextDatum(TL_DATUM);
  d.setFont(&lgfx::fonts::Font0);
  d.setTextColor(WHITE,BG);
  char stats[20];
  if(s.rssi>=0) snprintf(stats,sizeof(stats),"%ddBuV",s.rssi);
  else snprintf(stats,sizeof(stats),"RSSI --");
  d.drawString(stats,56,91);
  if(s.snr>=0) snprintf(stats,sizeof(stats),"SN%d",s.snr);
  else snprintf(stats,sizeof(stats),"SN--");
  d.drawString(stats,112,91);
  d.setTextColor(ready?GREEN:YELLOW,BG);
  d.drawString(ready?"VALID":"OFFLINE",158,91);
  d.setTextColor(WHITE,BG);
  d.drawString(s.stereo?"ST":"MO",224,91);

  // Station name and RadioText are centered, as in the original TinyRadio layout.
  d.setTextDatum(TC_DATUM);
  d.setFont(&lgfx::fonts::Font0);
  d.setTextColor(CYAN,BG);
  if(s.stationName[0]) d.drawString(s.stationName,160,104);
  d.setTextColor(WHITE,BG);
  if(s.radioText[0]) d.drawString(s.radioText,160,119);

  // Original 320-pixel frequency ruler with a fixed center marker at x=160.
  d.drawFastHLine(0,142,320,WHITE);
  const int32_t centerFreq=static_cast<int32_t>(s.frequencyKHz);
  for(int i=0;i<=20;i++) {
    const int px=i*16;
    const bool major=(i%4)==0;
    d.drawFastVLine(px,142,major?12:6,DIM);
    if(major) {
      const int32_t tick=centerFreq+(i-10)*50;
      char label[16];
      if(tick>=1000)
        snprintf(label,sizeof(label),"%ld.%ldM",static_cast<long>(tick/1000),
                 static_cast<long>((tick%1000)/100));
      else
        snprintf(label,sizeof(label),"%ldk",static_cast<long>(tick));
      d.setTextColor(WHITE,BG);
      d.setTextDatum(TC_DATUM);
      d.drawString(label,px,155);
    }
  }
  d.fillCircle(160,142,6,RED);
  d.setTextDatum(TL_DATUM);
}
} // namespace SWLayouts
