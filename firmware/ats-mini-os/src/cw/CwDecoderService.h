#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "CwDecoder.h"

class CwDecoderService {
public:
  bool begin();
  bool enter(bool wifiOff);
  void exit();
  bool active() const { return active_; }
  bool wifiBlocked() const { return wifiBlocked_; }
  void clearText();
  void update();
  const char* text() const { return shownText_; }
  bool signal() const { return signal_; }
  uint16_t audioLevel() const { return audioLevel_; }
  int wpm() const { return wpm_; }
private:
  static void taskEntry(void* arg);
  void sampleTask();
  static constexpr uint8_t kPin=11;
  static constexpr uint32_t kSampleRate=4000;
  static constexpr uint8_t kBlock=24;
  CwDecoder decoder_;
  SemaphoreHandle_t mutex_=nullptr;
  volatile bool active_=false;
  bool initialized_=false,wifiBlocked_=false,signal_=false;
  volatile uint16_t audioLevel_=0;
  char shownText_[CwDecoder::TEXT_MAX+1]={0};
  int wpm_=0;
  uint32_t lastUiMs_=0,lastToneMs_=0;
};
