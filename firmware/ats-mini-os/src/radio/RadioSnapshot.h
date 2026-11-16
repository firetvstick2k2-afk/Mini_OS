#pragma once
#include <Arduino.h>
#include "BandManager.h"

// Immutable-style data contract for publishing tuner state to UI and services.
// Only the future radio-core/RDS-owner modules should produce snapshots.
struct RadioSnapshot {
  uint32_t frequencyKHz = 0;
  char bandName[8] = "VHF";
  RadioModulation modulation = RadioModulation::FM;
  uint32_t bandMinimumKHz = 87500;
  uint32_t bandMaximumKHz = 108000;
  uint16_t tuningStepKHz = 100;
  int16_t bfoOffsetHz = 0;
  int16_t rssi = -1;
  int16_t snr = -1;
  uint16_t piCode = 0;
  uint8_t pty = 0;
  bool rdsTpValid = false;
  bool rdsTp = false;
  bool rdsTaValid = false;
  bool rdsTa = false;
  bool rdsMusicSpeechValid = false;
  bool rdsMusicSpeech = false;
  bool rdsDiValid = false;
  bool rdsDi = false;
  bool rdsEccValid = false;
  uint8_t rdsEcc = 0;
  uint8_t rdsAfCount = 0;
  uint32_t rdsAfKHz[25] = {};
  bool rdsEonValid = false;
  uint16_t rdsEonPi = 0;
  bool rdsEonTp = false, rdsEonTa = false;
  uint8_t rdsEonPty = 0, rdsEonAfCount = 0;
  uint32_t rdsEonAfKHz[25] = {};
  char rdsEonPs[9] = {};
  uint16_t rdsOdaAid = 0;
  uint8_t rdsOdaGroupType = 0xFF, rdsOdaGroupVersion = 0xFF;
  bool rdsRtPlusValid[2] = {};
  bool rdsRtPlusItemToggle = false, rdsRtPlusItemRunning = false;
  uint8_t rdsRtPlusType[2] = {}, rdsRtPlusStart[2] = {}, rdsRtPlusLength[2] = {};
  char rdsRtPlusText[2][33] = {};
  bool stereo = false;
  bool rdsAvailable = false;
  bool rdsClockValid = false;
  uint8_t rdsClockHour = 0;
  uint8_t rdsClockMinute = 0;
  char stationName[9] = {};
  char radioText[65] = {};
  uint32_t generation = 0;
};
