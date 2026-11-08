#pragma once
#include <Arduino.h>
#include "../radio/RadioSnapshot.h"
#include <string.h>
#include "DisplayStatusBar.h"

// ATS Mini DEV LW/MW/KW layouts adapted to the OS RadioSnapshot contract.
// Each renderer is isolated from the FM/DAB and TEF6686 renderers.
namespace SWLayouts {
constexpr uint16_t BG=0x0000, WHITE=0xFFFF, GREEN=0x07E0, CYAN=0x07FF;
constexpr uint16_t YELLOW=0xFFE0, RED=0xF800, GREY=0x7BEF, BLUE=0x001F;

template<class C> void header(C& d,const RadioSnapshot& s,bool wifi) {
  drawNetworkBatteryStatus(d, wifi);
  d.setTextDatum(MC_DATUM); d.setTextColor(GREEN,BG);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b); d.drawString(s.bandName,160,3);
  d.setTextDatum(TL_DATUM);
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

#if 0  // Retired legacy LW/MW/KW layout; kept as reference for future redesign.
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
#endif
#if 0  // Retired legacy LW/MW/KW layout; kept as reference for future redesign.
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
#endif
#if 0  // Retired legacy LW/MW/KW layout; kept as reference for future redesign.
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
#endif
#if 0  // Retired legacy LW/MW/KW layout; kept as reference for future redesign.
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
#endif
template<class C> void drawDecoder(C& d,const RadioSnapshot& s,bool ready,bool wifi) {
  d.fillScreen(BG);
  d.setTextDatum(TC_DATUM); d.setTextColor(WHITE,BG);
  d.setFont(&lgfx::fonts::Orbitron_Light_24);
  const char* mode=s.modulation==RadioModulation::FM?"FM":(s.modulation==RadioModulation::AM?"AM":(s.modulation==RadioModulation::USB?"USB":"LSB"));
  char bandMode[24]; snprintf(bandMode,sizeof(bandMode),"%s %s",s.bandName,mode);
  drawNetworkBatteryStatus(d, wifi, 8);
  d.drawString(bandMode,160,22);
  d.setTextDatum(TL_DATUM); d.setFont(&lgfx::fonts::Font7); d.setTextColor(WHITE,BG);
  char freq[24];
  if(s.modulation==RadioModulation::FM) snprintf(freq,sizeof(freq),"%lu.%02lu",(unsigned long)(s.frequencyKHz/1000UL),(unsigned long)((s.frequencyKHz%1000UL)/10UL));
  else if(s.modulation==RadioModulation::USB || s.modulation==RadioModulation::LSB) snprintf(freq,sizeof(freq),"%lu.000",(unsigned long)s.frequencyKHz);
  else snprintf(freq,sizeof(freq),"%lu",(unsigned long)s.frequencyKHz);
  d.setTextSize(s.modulation==RadioModulation::FM?0.78f:((s.modulation==RadioModulation::USB || s.modulation==RadioModulation::LSB)?0.56f:0.68f));
  d.setTextDatum(MC_DATUM); d.drawString(freq,160,43); d.setTextSize(1.0f);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b); d.setTextColor(WHITE,BG);
  d.drawString(s.modulation==RadioModulation::FM?"MHz":"kHz",245,67);
  if(s.modulation==RadioModulation::FM) {
    d.setTextDatum(TL_DATUM); d.setTextColor(s.stereo?GREEN:RED,BG);
    d.setFont(&lgfx::fonts::FreeSans9pt7b); d.drawString(s.stereo?"STEREO":"MONO",240,62);
  }
  d.setTextDatum(TC_DATUM); d.setFont(&lgfx::fonts::FreeSansBold9pt7b); d.setTextColor(GREEN,BG);
  const char* station=s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE");
  if(station && *station) d.drawString(station,145,90);
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
  const char* bands[]={"LW","MW","SW","VHF","BT","WEB"}; const int bx[]={2,37,72,107,142,177};
  for(int i=0;i<6;i++) {
    const bool selected=(i==0 && strncmp(s.bandName,"LW",2)==0)||(i==1 && strncmp(s.bandName,"MW",2)==0)||(i==2 && (strncmp(s.bandName,"SW",2)==0||strncmp(s.bandName,"KW",2)==0))||(i==3 && strncmp(s.bandName,"VHF",3)==0);
    const uint16_t color=selected?CYAN:DIM; d.drawRect(bx[i],3,32,17,color);
    d.setTextColor(color,BG); d.setTextDatum(MC_DATUM); d.setFont(&lgfx::fonts::Font0); d.drawString(bands[i],bx[i]+16,11);
  }
  drawNetworkBatteryStatus(d, wifi, 218);
  char freq[32];
  if(s.modulation==RadioModulation::FM) snprintf(freq,sizeof(freq),"%lu.%02lu MHz",(unsigned long)(s.frequencyKHz/1000UL),(unsigned long)((s.frequencyKHz%1000UL)/10UL));
  else if(s.modulation==RadioModulation::USB||s.modulation==RadioModulation::LSB) snprintf(freq,sizeof(freq),"%lu.000 kHz",(unsigned long)s.frequencyKHz);
  else snprintf(freq,sizeof(freq),"%lu kHz",(unsigned long)s.frequencyKHz);
  d.setTextColor(YELLOW,BG); d.setTextDatum(TL_DATUM); d.setFont(&lgfx::fonts::Font7);
  d.setTextSize((s.modulation==RadioModulation::USB||s.modulation==RadioModulation::LSB)?0.58f:0.82f);
  d.setTextDatum(MC_DATUM); d.drawString(freq,160,36); d.setTextSize(1.0f);
  const char* modes[]={"FM","AM","LSB","USB"}; const RadioModulation vals[]={RadioModulation::FM,RadioModulation::AM,RadioModulation::LSB,RadioModulation::USB}; const int mx[]={3,43,83,123};
  for(int i=0;i<4;i++){bool active=s.modulation==vals[i];uint16_t col=active?CYAN:DIM;d.drawRect(mx[i],66,36,21,col);d.setTextColor(col,BG);d.setTextDatum(MC_DATUM);d.setFont(&lgfx::fonts::Font0);d.drawString(modes[i],mx[i]+18,76);}
  // BW/AGC/SM controls are not displayed because these states are not exposed by RadioSnapshot.
  const int rssi=constrain((int)s.rssi,0,80); for(int i=0;i<8;i++)d.fillRect(5+i*6,93,4,8,i<(rssi/10)?GREEN:DIM);
  d.setTextDatum(TL_DATUM);d.setFont(&lgfx::fonts::Font0);d.setTextColor(WHITE,BG);char stats[20];
  if(s.rssi>=0)snprintf(stats,sizeof(stats),"%ddBuV",s.rssi);else snprintf(stats,sizeof(stats),"RSSI --");d.drawString(stats,56,91);
  if(s.snr>=0)snprintf(stats,sizeof(stats),"SN%d",s.snr);else snprintf(stats,sizeof(stats),"SN--");d.drawString(stats,105,91);
  d.setTextColor(ready?GREEN:YELLOW,BG);d.drawString(ready?"VALID":"OFFLINE",145,91);
  d.setTextColor(WHITE,BG);const char* status=s.modulation==RadioModulation::FM?(s.stereo?"STEREO":"MONO"):(s.modulation==RadioModulation::AM?"AM":(s.modulation==RadioModulation::USB?"USB":"LSB"));d.drawString(status,198,91);d.drawString("OK",230,91);d.drawString("RF0",256,91);
  const char* station=s.stationName[0]?s.stationName:(ready?"":"RADIO OFFLINE");d.setTextDatum(TC_DATUM);d.setFont(&lgfx::fonts::Font0);d.setTextColor(CYAN,BG);if(station&&*station)d.drawString(station,160,104);
  d.setTextColor(WHITE,BG);if(s.modulation==RadioModulation::FM&&s.radioText[0])d.drawString(s.radioText,160,119);
  d.drawFastHLine(0,142,320,WHITE);const int32_t centerFreq=(int32_t)s.frequencyKHz;
  for(int i=0;i<=20;i++){int px=i*16;bool major=(i%4)==0;d.drawFastVLine(px,142,major?12:6,DIM);if(major){int32_t delta=(i-10)*(s.modulation==RadioModulation::FM?100:50);int32_t tick=centerFreq+delta;char label[16];
    if(s.modulation==RadioModulation::FM)snprintf(label,sizeof(label),"%ld.%02ld",(long)(tick/1000),(long)((tick%1000)/10));
    else if(tick>=1000)snprintf(label,sizeof(label),"%ld.%ldM",(long)(tick/1000),(long)((tick%1000)/100));else snprintf(label,sizeof(label),"%ldk",(long)tick);
    d.setTextColor(WHITE,BG);d.setTextDatum(TC_DATUM);d.drawString(label,px,155);}}
  d.fillCircle(160,142,6,RED);d.setTextDatum(TL_DATUM);
}

template<class C> void drawTef6686SW(C& d, const RadioSnapshot& s, bool ready, bool wifi,
                                     const char* eibiStation, const char* eibiStatus,
                                     bool eibiAvailable, bool eibiDownloading) {
  constexpr uint16_t TEF_BLUE=0x001F, TEF_GREEN=0xBFE0, TEF_CYAN=0x07FF;
  constexpr uint16_t TEF_GREY=0x4208, TEF_YELLOW=0xFFE0;
  d.fillScreen(BG);
  drawNetworkBatteryStatus(d, wifi);
  d.setTextDatum(MC_DATUM);
  d.setTextColor(TEF_GREEN, BG);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.drawString(s.bandName, 160, 4);
  d.setTextDatum(TL_DATUM);

  d.drawRoundRect(8, 25, 54, 20, 3, TEF_BLUE);
  d.setTextDatum(MC_DATUM);
  d.setTextColor(TEF_GREEN, BG);
  const char* mode = s.modulation == RadioModulation::AM ? "AM" :
                     s.modulation == RadioModulation::USB ? "USB" : "LSB";
  d.drawString(mode, 35, 35);
  d.setTextDatum(TL_DATUM);

  char freq[24];
  if (s.modulation == RadioModulation::USB || s.modulation == RadioModulation::LSB)
    snprintf(freq, sizeof(freq), "%lu.000", static_cast<unsigned long>(s.frequencyKHz));
  else
    snprintf(freq, sizeof(freq), "%lu", static_cast<unsigned long>(s.frequencyKHz));
  d.setFont(&lgfx::fonts::Font7);
  d.setTextColor(TEF_GREEN, BG);
  d.setTextSize((s.modulation == RadioModulation::USB || s.modulation == RadioModulation::LSB) ? 0.62f : 0.78f);
  d.setTextDatum(MC_DATUM);
  d.drawString(freq, 185, 47);
  d.setTextSize(1.0f);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.setTextColor(TEF_GREEN, BG);
  d.drawString("kHz", 278, 66);
  d.setTextDatum(TL_DATUM);

  // Compact TEF-style signal meter with the familiar green/yellow/red scale.
  constexpr int meterX=10, meterY=77, meterW=300, barX=43, barY=88, barW=250;
  d.drawRect(meterX, meterY, meterW, 31, TEF_BLUE);
  d.setFont(&lgfx::fonts::FreeSansBold9pt7b);
  d.setTextColor(TEF_GREEN, BG);
  d.drawString("S", 18, 83);
  d.fillRect(barX, barY, barW, 8, TEF_GREY);
  const int value=constrain(static_cast<int>(s.rssi),0,60);
  const int fill=map(value,0,60,0,barW);
  const int greenEnd=barW*65/100, yellowEnd=barW*82/100;
  if(fill>0)d.fillRect(barX,barY,min(fill,greenEnd),8,TEF_GREEN);
  if(fill>greenEnd)d.fillRect(barX+greenEnd,barY,min(fill,yellowEnd)-greenEnd,8,TEF_YELLOW);
  if(fill>yellowEnd)d.fillRect(barX+yellowEnd,barY,fill-yellowEnd,8,RED);

  // EiBi PS is deliberately independent from RadioSnapshot stationName/RDS.
  constexpr int psX=10, psY=114, psW=300, psH=22;
  d.drawRect(psX,psY,psW,psH,TEF_BLUE);
  d.setFont(&lgfx::fonts::FreeSans9pt7b);
  d.setTextColor(TEF_CYAN,BG);
  d.setTextDatum(TL_DATUM);
  d.drawString("PS:",psX+5,psY+3);
  const char* ps=(eibiStation && eibiStation[0]) ? eibiStation :
                 (eibiAvailable ? "" : "EiBi not loaded");
  if(ps[0]){
    d.setTextColor(TEF_GREEN,BG);
    d.setClipRect(psX+38,psY+1,psW-43,psH-2);
    d.drawString(ps,psX+40,psY+3);
    d.clearClipRect();
  }

  // RT is used only for the EiBi transfer message; never borrow FM RDS text.
  constexpr int rtX=10, rtY=141, rtW=300, rtH=22;
  d.drawRect(rtX,rtY,rtW,rtH,TEF_BLUE);
  d.setTextColor(TEF_CYAN,BG);
  d.drawString("RT:",rtX+5,rtY+3);
  const char* rt = eibiDownloading ? "Downloading EiBi Schedules ..." :
    ((!eibiAvailable && eibiStatus && strncmp(eibiStatus,"EiBi:",5)==0) ? eibiStatus : "");
  if(rt[0]){
    d.setTextColor(eibiDownloading ? TEF_YELLOW : RED,BG);
    d.setClipRect(rtX+38,rtY+1,rtW-43,rtH-2);
    d.drawString(rt,rtX+40,rtY+3);
    d.clearClipRect();
  }
  (void)ready;
}

} // namespace SWLayouts
