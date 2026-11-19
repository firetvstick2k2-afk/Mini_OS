#include "SpectrumScannerService.h"
#include <stdlib.h>
#include <string.h>

namespace {
constexpr uint16_t kAllSteps[]={5,10,25,50,100,250,500,1000};
constexpr RadioModulation kAllModes[]={RadioModulation::AM,RadioModulation::USB,RadioModulation::LSB};
constexpr const char* kModeNames[]={"AM","USB","LSB"};
constexpr const char* kVhfKeys[]={"1","2","3","BS","4","5","6","0","7","8","9",".","OK","MHz","","CLOSE"};
constexpr const char* kAllKeys[]={"1","2","3","BS","4","5","6","0","7","8","9",".","OK","STEP","","CLOSE"};
}
const char* const* SpectrumScannerService::keys() const { return kind_==Kind::VhfFm?kVhfKeys:kAllKeys; }
const char* SpectrumScannerService::modeName() const { return kind_==Kind::VhfFm?"FM":kModeNames[modeIndex_%3]; }

void SpectrumScannerService::open() {
  state_=State::Input; count_=planned_=selected_=scanIndex_=0;
  input_[0][0]=input_[1][0]=0; inputStage_=0; keyIndex_=5;
  inputError_=false; savedValid_=false;
  if(kind_==Kind::VhfFm){startKHz_=88000;endKHz_=108000;stepKHz_=100;modeIndex_=0;}
  else {startKHz_=7100;endKHz_=7600;stepIndex_=0;stepKHz_=kAllSteps[0];modeIndex_=0;}
}
bool SpectrumScannerService::rotateInput(int16_t delta) {
  if(state_!=State::Input||!delta)return false;
  int next=static_cast<int>(keyIndex_)+(delta>0?1:-1);
  const int max=kind_==Kind::VhfFm?15:16;
  if(next<0)next=max;if(next>max)next=0;
  if(next==14){next+=(delta>0?1:-1);if(next<0)next=max;if(next>max)next=0;}
  keyIndex_=static_cast<uint8_t>(next);return true;
}
bool SpectrumScannerService::parseValue(uint32_t& value) const {
  const char* src=input_[inputStage_];if(!src[0])return false;
  if(kind_==Kind::VhfFm){
    char normalized[12];size_t n=strlen(src);if(n>=sizeof(normalized))return false;
    for(size_t i=0;i<=n;++i)normalized[i]=src[i]==','?'.':src[i];
    const char* dot=strchr(normalized,'.');if(dot&&strlen(dot+1)>2)return false;
    char* end=nullptr;const float mhz=strtof(normalized,&end);
    if(end==normalized||*end||mhz<88.0f||mhz>108.0f)return false;
    value=static_cast<uint32_t>(mhz*1000.0f+0.5f);
  } else {
    char* end=nullptr;const long parsed=strtol(src,&end,10);
    if(end==src||*end||parsed<100||parsed>30000)return false;
    value=static_cast<uint32_t>(parsed);
  }
  return value>=minKHz()&&value<=maxKHz();
}
bool SpectrumScannerService::clickInput(bool shortPress,RadioCore& radio) {
  if(state_!=State::Input)return false;
  if(!shortPress){cancel(radio);return true;}
  if(kind_==Kind::AllBand&&keyIndex_==16){modeIndex_=(modeIndex_+1)%3;return true;}
  const char* selectedKey=keys()[keyIndex_];
  char* value=input_[inputStage_];const size_t len=strlen(value);inputError_=false;
  if(strcmp(selectedKey,"CLOSE")==0){cancel(radio);return true;}
  if(strcmp(selectedKey,"BS")==0){if(len)value[len-1]=0;return true;}
  if(strcmp(selectedKey,"STEP")==0&&kind_==Kind::AllBand){stepIndex_=(stepIndex_+1)%8;stepKHz_=kAllSteps[stepIndex_];return true;}
  if(strcmp(selectedKey,"MHz")==0)return true;
  if(strcmp(selectedKey,".")==0&&kind_==Kind::AllBand)return true;
  if(strcmp(selectedKey,"OK")==0){
    uint32_t parsed=0;if(!parseValue(parsed)){inputError_=true;return true;}
    if(inputStage_==0){startKHz_=parsed;inputStage_=1;input_[1][0]=0;keyIndex_=5;return true;}
    endKHz_=parsed;
    if(endKHz_<=startKHz_){inputError_=true;return true;}
    if(kind_==Kind::VhfFm){if((endKHz_-startKHz_)%100!=0||(endKHz_-startKHz_)/100+1>kMaxPoints){inputError_=true;return true;}}
    else if(endKHz_-startKHz_>1000||(endKHz_-startKHz_)%stepKHz_!=0||(endKHz_-startKHz_)/stepKHz_+1>kMaxPoints||
            (modeIndex_!=0&&startKHz_<1800)){inputError_=true;return true;}
    return beginSweep(radio);
  }
  if(len<sizeof(input_[0])-1&&((selectedKey[0]>='0'&&selectedKey[0]<='9')||(kind_==Kind::VhfFm&&strcmp(selectedKey,".")==0))){
    value[len]=selectedKey[0];value[len+1]=0;
  }
  return true;
}
bool SpectrumScannerService::beginSweep(RadioCore& radio) {
  if(kind_==Kind::VhfFm){
    const uint8_t region=radio.fmRegion();
    const uint32_t fmMin=region==2?76000UL:(region==1?87900UL:87500UL);
    const uint32_t fmMax=region==2?95000UL:(region==1?107900UL:108000UL);
    if(startKHz_<fmMin||endKHz_>fmMax){inputError_=true;return false;}
  }
  saved_=radio.captureState();savedValid_=true;
  const uint8_t targetBand=kind_==Kind::VhfFm?0:1;
  if(!radio.selectBand(targetBand)){state_=State::Error;restore(radio);return false;}
  const RadioModulation mode=kind_==Kind::VhfFm?RadioModulation::FM:kAllModes[modeIndex_];
  if(!radio.setModulation(mode)||!radio.setBfoOffsetHz(0)||!radio.setVolume(0)||
     !radio.beginSpectrumScan()){state_=State::Error;restore(radio);return false;}
  spectrumScanActive_=true;
  if(!radio.tuneSpectrumScan(startKHz_)){state_=State::Error;restore(radio);return false;}
  planned_=static_cast<uint16_t>((endKHz_-startKHz_)/(kind_==Kind::VhfFm?100:stepKHz_)+1);
  count_=scanIndex_=selected_=0;state_=State::Running;tuneStartedMs_=lastTunePollMs_=millis();
  return true;
}
void SpectrumScannerService::restore(RadioCore& radio){
  if(spectrumScanActive_){radio.endSpectrumScan();spectrumScanActive_=false;}
  if(savedValid_){radio.restoreState(saved_);savedValid_=false;}
}
void SpectrumScannerService::cancel(RadioCore& radio){if(state_==State::Running||state_==State::Done||state_==State::Error)restore(radio);state_=State::Aborted;}
void SpectrumScannerService::tick(RadioCore& radio) {
  if(state_!=State::Running||!radio.isReady())return;
  const uint32_t now=millis();
  if(now-lastTunePollMs_<10)return;
  lastTunePollMs_=now;
  const bool tuneComplete=radio.spectrumTuneComplete();
  if(!tuneComplete&&now-tuneStartedMs_<350)return;
  radio.refreshSignalQuality();
  const RadioSnapshot& s=radio.snapshot();
  Point& p=points_[scanIndex_];p.frequencyKHz=startKHz_+static_cast<uint32_t>(scanIndex_)*(kind_==Kind::VhfFm?100:stepKHz_);
  p.rssi=static_cast<uint8_t>(constrain(s.rssi,0,255));p.snr=static_cast<uint8_t>(constrain(s.snr,0,255));
  ++scanIndex_;count_=scanIndex_;
  if(scanIndex_>=planned_){state_=State::Done;selected_=count_/2;restore(radio);return;}
  if(!radio.tuneSpectrumScan(startKHz_+static_cast<uint32_t>(scanIndex_)*(kind_==Kind::VhfFm?100:stepKHz_))){state_=State::Error;restore(radio);return;}
  tuneStartedMs_=lastTunePollMs_=millis();
}
void SpectrumScannerService::rotatePeak(int16_t delta,RadioCore& radio) {
  if(state_!=State::Done||count_<2||!delta)return;
  const int direction=delta>0?1:-1;
  if(count_==2) {
    // A two-point scan has no interior peak; allow direct selection of either sample.
    selected_=static_cast<uint16_t>((selected_+direction+count_)%count_);
  } else {
    uint8_t minR=255,maxR=0;
    for(uint16_t i=0;i<count_;++i){minR=min(minR,points_[i].rssi);maxR=max(maxR,points_[i].rssi);}
    const uint8_t threshold=static_cast<uint8_t>(minR+((uint16_t)(maxR-minR)*5)/100);
    int candidate=selected_;bool found=false;
    for(uint16_t n=0;n<count_;++n){candidate+=direction;if(candidate<0)candidate=count_-1;if(candidate>=count_)candidate=0;if(candidate==0||candidate==count_-1)continue;
      const uint8_t here=points_[candidate].rssi;
      if(here>=threshold&&here>=points_[candidate-1].rssi&&here>points_[candidate+1].rssi){selected_=candidate;found=true;break;}}
    if(!found){int best=-1;uint8_t bestR=0;candidate=selected_;
      for(uint16_t n=0;n<count_-1;++n){candidate+=direction;if(candidate<0)candidate=count_-1;if(candidate>=count_)candidate=0;if(candidate==selected_)break;if(candidate==0||candidate==count_-1)continue;if(best<0||points_[candidate].rssi>bestR){best=candidate;bestR=points_[candidate].rssi;}}
      if(best>=0)selected_=static_cast<uint16_t>(best);
    }
  }
  if(selected_<count_&&radio.selectBand(kind_==Kind::VhfFm?0:1)){
    radio.setModulation(kind_==Kind::VhfFm?RadioModulation::FM:kAllModes[modeIndex_]);
    radio.setBfoOffsetHz(0);
    radio.tune(points_[selected_].frequencyKHz);
  }
}
bool SpectrumScannerService::accept(RadioCore& radio) {
  if(state_!=State::Done||selected_>=count_)return false;
  const uint32_t frequency=points_[selected_].frequencyKHz;
  const uint8_t band=kind_==Kind::VhfFm?0:1;
  if(!radio.selectBand(band))return false;
  const RadioModulation mode=kind_==Kind::VhfFm?RadioModulation::FM:kAllModes[modeIndex_];
  if(!radio.setModulation(mode)||!radio.setBfoOffsetHz(0)||!radio.tune(frequency)){restore(radio);return false;}
  savedValid_=false;state_=State::Idle;return true;
}
