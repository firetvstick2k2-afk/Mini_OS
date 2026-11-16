#pragma once
#include <Arduino.h>

static constexpr uint8_t RDS_MAX_EON_NETWORKS = 8;
static constexpr uint8_t RDS_MAX_AF_METHOD_B_LISTS = 8;
static constexpr uint8_t RDS_MAX_AF_METHOD_B_ENTRIES = 12;

struct RdsAfMethodBList {
  bool valid = false;
  uint32_t tuningKHz = 0;
  uint8_t afCount = 0;
  uint32_t afKHz[RDS_MAX_AF_METHOD_B_ENTRIES] = {};
};

struct RdsEonNetwork {
  bool valid = false;
  uint16_t pi = 0;
  bool tp = false;
  bool ta = false;
  uint8_t pty = 0;
  char ps[9] = {};
  uint8_t afCount = 0;
  uint32_t afKHz[25] = {};
  uint8_t afExpectedCount = 0;
  bool afLfMfPending = false;
  uint32_t mappedTuningKHz = 0;
  uint32_t mappedFmKHz[4] = {};
  bool mappedFmValid[4] = {};
  uint32_t mappedAmKHz = 0;
  bool mappedAmValid = false;
  uint32_t mappedOtherBandKHz = 0;
  bool mappedOtherBandValid = false;
  bool linkageValid = false;
  bool linkageActuator = false;
  bool extendedGeneric = false;
  bool internationalLink = false;
  uint16_t linkageSetNumber = 0;
};

struct RdsRtPlusTag {
  bool valid = false;
  uint8_t contentType = 0;
  uint8_t start = 0;
  // RT+ length marker is the number of additional characters after start.
  uint8_t length = 0;
  char text[65] = {};
};

struct RdsDecodedFields {
  bool tpValid = false, ptyValid = false, taValid = false;
  bool linkageActuatorValid = false, linkageActuator = false;
  bool msValid = false, diValid = false;
  bool eccValid = false, afUpdated = false, afMethodBUpdated = false;
  bool eonUpdated = false, rtPlusUpdated = false;
  bool odaUpdated = false, ptynUpdated = false;
  bool tp = false, ta = false, musicSpeech = false, decoderIdentification = false;
  bool rtPlusItemToggle = false, rtPlusItemRunning = false;
  uint8_t ecc = 0, pty = 0, afCount = 0;
  uint8_t afMethodBCount = 0;
  uint8_t eonCount = 0;
  char ptyn[9] = {};
  uint16_t odaAid = 0;
  uint8_t odaGroupType = 0xFF, odaGroupVersion = 0xFF;
  uint32_t afKHz[25] = {};
  RdsAfMethodBList afMethodBLists[RDS_MAX_AF_METHOD_B_LISTS] = {};
  // eon is retained as a compatibility view of the most recently updated PI.
  RdsEonNetwork eon;
  RdsEonNetwork eonNetworks[RDS_MAX_EON_NETWORKS] = {};
  RdsRtPlusTag rtPlus[2] = {};
};

class RdsDecoder {
public:
  void reset();
  void process(uint8_t groupType, uint8_t version, uint16_t blockB,
               uint16_t blockC, uint16_t blockD, uint16_t pi,
               uint32_t tunedFrequencyKHz,
               uint8_t blockCError, uint8_t blockDError,
               const char* radioText,
               RdsDecodedFields& out);
private:
  void addAlternativeFrequency(uint32_t frequencyKHz);
  void processAlternativeFrequencies(uint16_t blockC, RdsDecodedFields& out);
  void processAlternativeFrequencyByte(uint8_t code);
  void publishAf(RdsDecodedFields& out);
  void publishAfMethodB(RdsDecodedFields& out);
  void addAfMethodB(uint32_t tuningKHz, uint32_t alternativeKHz);
  void processEon(uint8_t version, uint16_t blockB, uint16_t blockC,
                  uint16_t blockD, uint16_t pi, uint32_t tunedFrequencyKHz,
                  uint8_t blockCError, uint8_t blockDError,
                  RdsDecodedFields& out);
  void addEonAf(uint8_t networkIndex, uint8_t code);
  void processPtyn(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                   uint8_t blockCError, uint8_t blockDError,
                   RdsDecodedFields& out);
  void processOda(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                  uint8_t blockCError, uint8_t blockDError,
                  RdsDecodedFields& out);
  struct OdaAssignment {
    bool valid = false;
    uint16_t aid = 0;
    uint8_t groupType = 0xFF;
    uint8_t groupVersion = 0xFF;
    bool rtForEnhancedText = false;
  };
  OdaAssignment odaAssignments_[8] = {};
  void processRtPlus(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                     const char* radioText, uint8_t blockCError,
                     uint8_t blockDError, RdsDecodedFields& out);
  uint32_t afKHz_[25] = {};
  uint8_t afCount_ = 0;
  // AF parser: 0=idle, 1=waiting to distinguish method A/B, 2=method A,
  // 3=method B pairs. Method B pairs always occupy one complete block C.
  uint8_t afState_ = 0;
  uint8_t afExpectedCount_ = 0;
  uint8_t afReceivedCount_ = 0;
  uint8_t afHeaderCode_ = 0;
  uint8_t afPairsExpected_ = 0;
  uint8_t afPairsReceived_ = 0;
  bool afLfMfPending_ = false;
  RdsAfMethodBList afMethodBLists_[RDS_MAX_AF_METHOD_B_LISTS] = {};
  uint8_t afMethodBCount_ = 0;
  uint8_t odaGroupType_ = 0xFF, odaGroupVersion_ = 0xFF;
  uint16_t rtPlusAid_ = 0;
  bool rtPlusForEnhancedText_ = false;
  char ptyn_[9] = {};
  uint8_t ptynSegments_ = 0;
  uint8_t ptynFlag_ = 0xFF;
  RdsEonNetwork eonNetworks_[RDS_MAX_EON_NETWORKS] = {};
};
