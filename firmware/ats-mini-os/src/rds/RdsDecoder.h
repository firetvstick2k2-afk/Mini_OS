#pragma once
#include <Arduino.h>

struct RdsEonNetwork {
  bool valid = false;
  uint16_t pi = 0;
  bool tp = false;
  bool ta = false;
  uint8_t pty = 0;
  char ps[9] = {};
  uint8_t afCount = 0;
  uint32_t afKHz[25] = {};
};

struct RdsRtPlusTag {
  bool valid = false;
  uint8_t contentType = 0;
  uint8_t start = 0;
  uint8_t length = 0;
  char text[33] = {};
};

struct RdsDecodedFields {
  bool tpValid = false, taValid = false, msValid = false, diValid = false;
  bool eccValid = false, afUpdated = false;
  bool eonUpdated = false, rtPlusUpdated = false;
  bool odaUpdated = false;
  bool tp = false, ta = false, musicSpeech = false, decoderIdentification = false;
  bool rtPlusItemToggle = false, rtPlusItemRunning = false;
  uint8_t ecc = 0, pty = 0, afCount = 0;
  uint16_t odaAid = 0;
  uint8_t odaGroupType = 0, odaGroupVersion = 0;
  uint32_t afKHz[25] = {};
  RdsEonNetwork eon;
  RdsRtPlusTag rtPlus[2];
};

class RdsDecoder {
public:
  void reset();
  void process(uint8_t groupType, uint8_t version, uint16_t blockB,
               uint16_t blockC, uint16_t blockD, uint16_t pi,
               uint8_t blockCError, const char* radioText,
               RdsDecodedFields& out);
private:
  void addAlternativeFrequency(uint8_t code);
  void processEon(uint8_t version, uint16_t blockB, uint16_t blockC,
                  uint16_t blockD, uint8_t blockCError, RdsDecodedFields& out);
  void processOda(uint16_t blockB, uint16_t blockD, RdsDecodedFields& out);
  void processRtPlus(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                     const char* radioText, RdsDecodedFields& out);
  uint32_t afKHz_[25] = {};
  uint8_t afCount_ = 0, expectedAfCount_ = 0;
  uint8_t odaGroupType_ = 0xFF, odaGroupVersion_ = 0xFF;
  uint16_t rtPlusAid_ = 0;
  RdsEonNetwork eonNetworks_[8] = {};
};
