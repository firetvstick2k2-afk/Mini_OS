#include "CwDecoderService.h"
#include <esp_timer.h>
#include <math.h>

bool CwDecoderService::begin() {
  if(initialized_)return true;
  mutex_=xSemaphoreCreateMutex();
  if(!mutex_)return false;
  decoder_.begin(kSampleRate,700.0f);
  if(xTaskCreatePinnedToCore(taskEntry,"cw-sampler",4096,this,2,nullptr,0)!=pdPASS){vSemaphoreDelete(mutex_);mutex_=nullptr;return false;}
  initialized_=true;
  return true;
}
bool CwDecoderService::enter(bool wifiOff) {
#if defined(LILYGO_SI473X)
  (void)wifiOff; wifiBlocked_=true; active_=false; return false;
#else
  if(!begin())return false;
  wifiBlocked_=!wifiOff;
  if(wifiBlocked_){active_=false;return false;}
  xSemaphoreTake(mutex_,portMAX_DELAY);decoder_.reset();shownText_[0]=0;wpm_=0;xSemaphoreGive(mutex_);
  audioLevel_=0;signal_=false;lastToneMs_=0;lastUiMs_=millis();
  pinMode(kPin,INPUT);active_=true;return true;
#endif
}
void CwDecoderService::exit(){active_=false;wifiBlocked_=false;}
void CwDecoderService::clearText(){if(!mutex_)return;xSemaphoreTake(mutex_,portMAX_DELAY);decoder_.clearText();shownText_[0]=0;xSemaphoreGive(mutex_);}
void CwDecoderService::update(){
  if(!active_||millis()-lastUiMs_<100)return;
  lastUiMs_=millis();
  xSemaphoreTake(mutex_,portMAX_DELAY);
  strncpy(shownText_,decoder_.getText(),sizeof(shownText_)-1);shownText_[sizeof(shownText_)-1]=0;
  wpm_=decoder_.getWpm();const bool key=decoder_.isKeyDown();const uint8_t level=decoder_.getLevel();
  xSemaphoreGive(mutex_);
  if(key&&level>20)lastToneMs_=millis();
  signal_=lastToneMs_&&millis()-lastToneMs_<1200;
}
void CwDecoderService::taskEntry(void* arg){static_cast<CwDecoderService*>(arg)->sampleTask();}
void CwDecoderService::sampleTask(){
  int16_t samples[kBlock];uint8_t count=0;int64_t base=0,next=0;uint64_t index=0;
  float dc=2048,dev=0;uint32_t devCount=0;
  for(;;){
    if(!active_){count=0;vTaskDelay(pdMS_TO_TICKS(25));continue;}
    if(!base){base=next=esp_timer_get_time();index=0;}
    while(active_&&esp_timer_get_time()<next)taskYIELD();
    if(!active_){base=0;continue;}
    ++index;next=base+(int64_t)(index*1000000ULL/kSampleRate);
    int value=analogRead(kPin);
    if(value<0)value=(int)dc;
    dc+=(value-dc)*0.001f;dev+=fabsf(value-dc);
    if(++devCount>=kSampleRate/2){audioLevel_=(uint16_t)(dev/devCount);dev=0;devCount=0;}
    samples[count++]=(int16_t)value;
    if(count>=kBlock){
      if(mutex_&&xSemaphoreTake(mutex_,0)==pdTRUE){decoder_.push(samples,count);xSemaphoreGive(mutex_);}
      count=0;
    }
    if(index%(kSampleRate/2)==0){vTaskDelay(1);base=next=esp_timer_get_time();index=0;}
    if(esp_timer_get_time()-next>10000){base=next=esp_timer_get_time();index=0;}
  }
}
