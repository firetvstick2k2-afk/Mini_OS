#include "FmPanoramaService.h"
#include <string.h>
bool FmPanoramaService::start(RadioCore& radio,uint32_t centerKHz) {
  if(state_==State::Running || !radio.isReady() || radio.snapshot().modulation!=RadioModulation::FM || centerKHz<88500 || centerKHz>107000) return false;
  startKHz_=centerKHz-1000; savedFrequencyKHz_=radio.snapshot().frequencyKHz;
  count_=0; memset(points_,0,sizeof(points_)); state_=State::Running; waitingForSample_=false; lastTuneMs_=0;
  Serial.printf("[FM PANORAMA] start center=%lu kHz range=%lu..%lu step=100 points=%u\n",(unsigned long)centerKHz,(unsigned long)startKHz_,(unsigned long)(startKHz_+2000),kPointCount);
  return true;
}
void FmPanoramaService::restore(RadioCore& radio) { if(savedFrequencyKHz_) radio.tune(savedFrequencyKHz_); waitingForSample_=false; }
void FmPanoramaService::stop(RadioCore& radio) { if(state_==State::Running){restore(radio);state_=State::Aborted;Serial.println("[FM PANORAMA] aborted; original frequency restored");} }
void FmPanoramaService::invalidate(){if(state_!=State::Running){state_=State::Idle;count_=0;memset(points_,0,sizeof(points_));}}
void FmPanoramaService::tick(RadioCore& radio) {
  if(state_!=State::Running)return;
  const uint32_t now=millis();
  if(!waitingForSample_){
    const uint32_t target=startKHz_+(uint32_t)count_*100;
    if(!radio.tune(target)){restore(radio);state_=State::Error;Serial.printf("[FM PANORAMA] tune failed %lu kHz\n",(unsigned long)target);return;}
    lastTuneMs_=now;waitingForSample_=true;return;
  }
  if(now-lastTuneMs_<560)return;
  const RadioSnapshot& s=radio.snapshot();
  points_[count_++]={s.frequencyKHz,(uint8_t)constrain(s.rssi,0,255),(uint8_t)constrain(s.snr,0,255)};
  waitingForSample_=false;
  if(count_>=kPointCount){restore(radio);state_=State::Done;Serial.printf("[FM PANORAMA] complete points=%u\n",count_);}
}
