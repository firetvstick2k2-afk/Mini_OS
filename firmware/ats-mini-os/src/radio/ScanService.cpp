#include "ScanService.h"
#include <string.h>

const ScanService::Point& ScanService::point(uint16_t index) const {
  static const Point empty{}; return index<count_?points_[index]:empty;
}
bool ScanService::start(RadioCore& radio) {
  if(state_==State::Running||!radio.isReady()) return false;
  const RadioSnapshot& s=radio.snapshot();
  stepKHz_=s.tuningStepKHz?s.tuningStepKHz:1;
  if(s.bandMaximumKHz<=s.bandMinimumKHz||!stepKHz_) return false;
  savedFrequencyKHz_=s.frequencyKHz;
  planned_=static_cast<uint16_t>((s.bandMaximumKHz-s.bandMinimumKHz)/stepKHz_+1);
  if(planned_>kMaxPoints) planned_=kMaxPoints;
  if(!planned_) return false;
  // Center the 256-point window on the tuned frequency when the band is wider.
  const uint32_t halfSpan=static_cast<uint32_t>(planned_-1)*stepKHz_/2;
  startKHz_=s.frequencyKHz>halfSpan?s.frequencyKHz-halfSpan:s.bandMinimumKHz;
  if(startKHz_<s.bandMinimumKHz) startKHz_=s.bandMinimumKHz;
  const uint32_t endKHz=startKHz_+static_cast<uint32_t>(planned_-1)*stepKHz_;
  if(endKHz>s.bandMaximumKHz) startKHz_=s.bandMaximumKHz-static_cast<uint32_t>(planned_-1)*stepKHz_;
  count_=0; memset(points_,0,sizeof(points_));
  state_=State::Running; waitingForSample_=false; lastTuneMs_=0;
  Serial.printf("[SCAN] start %lu..%lu kHz step=%lu points=%u\n",
    static_cast<unsigned long>(startKHz_),static_cast<unsigned long>(startKHz_+static_cast<uint32_t>(planned_-1)*stepKHz_),
    static_cast<unsigned long>(stepKHz_),planned_);
  return true;
}
void ScanService::restore(RadioCore& radio) {
  if(savedFrequencyKHz_) radio.tune(savedFrequencyKHz_);
  waitingForSample_=false;
}
void ScanService::stop(RadioCore& radio) {
  if(state_==State::Running) { restore(radio); state_=State::Aborted; }
}
void ScanService::tick(RadioCore& radio) {
  if(state_!=State::Running) return;
  const uint32_t now=millis();
  if(!waitingForSample_) {
    const uint32_t target=startKHz_+static_cast<uint32_t>(count_)*stepKHz_;
    if(!radio.tune(target)) { restore(radio); state_=State::Error; return; }
    lastTuneMs_=now; waitingForSample_=true; return;
  }
  // RadioCore refreshes RSSI/SNR on a 500 ms cadence; sample only after it.
  if(now-lastTuneMs_<560) return;
  const RadioSnapshot& s=radio.snapshot();
  points_[count_++]={s.frequencyKHz,static_cast<uint8_t>(constrain(s.rssi,0,255)),static_cast<uint8_t>(constrain(s.snr,0,255))};
  waitingForSample_=false;
  if(count_>=planned_) { restore(radio); state_=State::Done; Serial.printf("[SCAN] complete points=%u\n",count_); }
}
