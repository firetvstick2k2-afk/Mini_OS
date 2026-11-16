#pragma once
#include <Arduino.h>

// Region-independent RDS group field decoder. European RDS PTY numbering is
// the standard RDS numbering; this module deliberately does not apply RBDS.
struct RdsDecodedFields {
  bool tpValid = false;
  bool taValid = false;
  bool msValid = false;
  bool diValid = false;
  bool eccValid = false;
  bool afUpdated = false;
  bool tp = false;
  bool ta = false;
  bool musicSpeech = false;
  bool decoderIdentification = false;
  uint8_t ecc = 0;
  uint8_t pty = 0;
  uint8_t afCount = 0;
  uint32_t afKHz[25] = {};
};

class RdsDecoder {
public:
  void reset();
  void process(uint8_t groupType, uint8_t version, uint16_t blockB,
               uint16_t blockC, uint8_t blockCError, RdsDecodedFields& out);
private:
  void addAlternativeFrequency(uint8_t code);
  uint32_t afKHz_[25] = {};
  uint8_t afCount_ = 0;
  uint8_t expectedAfCount_ = 0;
};
