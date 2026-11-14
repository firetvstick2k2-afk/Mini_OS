#include "SeekService.h"

bool SeekService::start(RadioCore& radio, int8_t direction, uint8_t threshold) {
  if(state_==State::Running || !radio.isReady() || !direction) return false;
  const RadioSnapshot& s=radio.snapshot();
  minimumKHz_=s.bandMinimumKHz; maximumKHz_=s.bandMaximumKHz;
  stepKHz_=s.tuningStepKHz?s.tuningStepKHz:1;
  if(s.modulation==RadioModulation::LSB || s.modulation==RadioModulation::USB) {
    if(minimumKHz_<1800) minimumKHz_=1800;
    if(maximumKHz_>30000) maximumKHz_=30000;
  }
  if(maximumKHz_<=minimumKHz_) return false;
  originKHz_=s.frequencyKHz; lastCandidateKHz_=originKHz_;
  direction_=direction>0?1:-1; threshold_=threshold; attempts_=0;
  state_=State::Running; lastTuneMs_=millis();
  Serial.printf("[SEEK] start %s from %lu kHz threshold=%u\n",direction_>0?"up":"down",
    static_cast<unsigned long>(originKHz_),threshold_);
  return true;
}
void SeekService::tick(RadioCore& radio) {
  if(state_!=State::Running) return;
  const uint32_t now=millis();
  if(now-lastTuneMs_<110) return;
  const RadioSnapshot& s=radio.snapshot();
  if(attempts_>0 && s.rssi>=threshold_) { state_=State::Found; Serial.printf("[SEEK] found %lu kHz RSSI=%d\n",static_cast<unsigned long>(s.frequencyKHz),s.rssi); return; }
  if(attempts_>=static_cast<uint16_t>((maximumKHz_-minimumKHz_)/stepKHz_+1)) {
    radio.tune(originKHz_); state_=State::NotFound; return;
  }
  int64_t next=static_cast<int64_t>(lastCandidateKHz_)+static_cast<int64_t>(direction_)*stepKHz_;
  if(next>maximumKHz_) next=minimumKHz_;
  if(next<minimumKHz_) next=maximumKHz_;
  lastCandidateKHz_=static_cast<uint32_t>(next);
  if(!radio.tune(lastCandidateKHz_)) { radio.tune(originKHz_); state_=State::Error; return; }
  ++attempts_; lastTuneMs_=now;
}
