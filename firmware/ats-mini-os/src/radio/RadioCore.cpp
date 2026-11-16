#include "RadioCore.h"
#include <stdio.h>
#include <string.h>

namespace {
void copyField(char* dst, size_t capacity, const char* src) {
  if (!capacity || !src) return;
  size_t i = 0;
  for (; i + 1 < capacity && src[i] != '\0'; ++i) dst[i] = src[i];
  dst[i] = '\0';
}
void clearRdsSnapshot(RadioSnapshot& s) {
  s.piCode = 0;
  s.pty = 0;
  s.rdsTpValid = s.rdsTaValid = s.rdsMusicSpeechValid = s.rdsDiValid = false;
  s.rdsTp = s.rdsTa = s.rdsMusicSpeech = s.rdsDi = false;
  s.rdsEccValid = false;
  s.rdsEcc = 0;
  s.rdsAfCount = 0;
  memset(s.rdsAfKHz, 0, sizeof(s.rdsAfKHz));
  s.rdsEonValid = false;
  s.rdsEonPi = 0;
  s.rdsEonTp = s.rdsEonTa = false;
  s.rdsEonPty = s.rdsEonAfCount = 0;
  memset(s.rdsEonAfKHz, 0, sizeof(s.rdsEonAfKHz));
  memset(s.rdsEonPs, 0, sizeof(s.rdsEonPs));
  s.rdsOdaAid = 0;
  s.rdsOdaGroupType = s.rdsOdaGroupVersion = 0xFF;
  s.rdsRtPlusItemToggle = s.rdsRtPlusItemRunning = false;
  memset(s.rdsRtPlusValid, 0, sizeof(s.rdsRtPlusValid));
  memset(s.rdsRtPlusType, 0, sizeof(s.rdsRtPlusType));
  memset(s.rdsRtPlusStart, 0, sizeof(s.rdsRtPlusStart));
  memset(s.rdsRtPlusLength, 0, sizeof(s.rdsRtPlusLength));
  memset(s.rdsRtPlusText, 0, sizeof(s.rdsRtPlusText));
  s.rdsAvailable = false;
  s.rdsClockValid = false;
  s.rdsClockHour = s.rdsClockMinute = 0;
  s.stationName[0] = '\0';
  s.radioText[0] = '\0';
}

void publishBand(RadioSnapshot& snapshot, const RadioBandDefinition& band) {
  strncpy(snapshot.bandName, band.name, sizeof(snapshot.bandName) - 1);
  snapshot.bandName[sizeof(snapshot.bandName) - 1] = '\0';
  snapshot.modulation = band.modulation;
  snapshot.bandMinimumKHz = band.minimumKHz;
  snapshot.bandMaximumKHz = band.maximumKHz;
  snapshot.tuningStepKHz = band.stepKHz;
}
}

bool RadioCore::begin(uint32_t initialFrequencyKHz) {
  BandManager::select(0);
  ready_ = tuner_.begin(initialFrequencyKHz);
  if (!ready_) return false;
  const auto& band = BandManager::current();
  publishBand(snapshot_, band);
  if (band.modulation == RadioModulation::FM) {
    const uint8_t region = tuner_.fmRegion();
    snapshot_.bandMinimumKHz = region == 2 ? 76000 : (region == 1 ? 87900 : 87500);
    snapshot_.bandMaximumKHz = region == 2 ? 95000 : (region == 1 ? 107900 : 108000);
    snapshot_.tuningStepKHz = region == 1 ? 200 : 100;
  }
  tuner_.refreshSignalQuality();
  snapshot_.frequencyKHz = tuner_.frequencyKHz();
  snapshot_.bfoOffsetHz = tuner_.bfoOffsetHz();
  snapshot_.rssi = tuner_.rssi();
  snapshot_.snr = tuner_.snr();
  snapshot_.stereo = tuner_.stereo();
  snapshot_.generation = ++generation_;
  lastPollMs_ = millis();
  lastRdsPollMs_ = lastPollMs_;
  Serial.printf("[RADIO] Ready; band=%s start=%lu kHz\n",
                snapshot_.bandName, static_cast<unsigned long>(snapshot_.frequencyKHz));
  return true;
}

void RadioCore::update() {
  if (!ready_) return;
  const uint32_t now = millis();
  if (now - lastPollMs_ >= 500) {
    lastPollMs_ = now;
    tuner_.refreshSignalQuality();
    snapshot_.frequencyKHz = tuner_.frequencyKHz();
    snapshot_.rssi = tuner_.rssi();
    snapshot_.snr = tuner_.snr();
    snapshot_.stereo = tuner_.stereo();
  }
  if (snapshot_.modulation != RadioModulation::FM) return;
  if (now - lastRdsPollMs_ >= kRdsPollIntervalMs) {
    lastRdsPollMs_ = now;
    TunerRdsData rds;
    if (tuner_.pollRds(rds)) {
      if (rds.piValid) snapshot_.piCode = rds.piCode;
      if (rds.ptyValid) snapshot_.pty = rds.pty;
      if (rds.tpValid) { snapshot_.rdsTpValid = true; snapshot_.rdsTp = rds.tp; }
      if (rds.taValid) { snapshot_.rdsTaValid = true; snapshot_.rdsTa = rds.ta; }
      if (rds.msValid) { snapshot_.rdsMusicSpeechValid = true; snapshot_.rdsMusicSpeech = rds.musicSpeech; }
      if (rds.diValid) { snapshot_.rdsDiValid = true; snapshot_.rdsDi = rds.decoderIdentification; }
      if (rds.eccValid) { snapshot_.rdsEccValid = true; snapshot_.rdsEcc = rds.eccCode; }
      if (rds.afUpdated) {
        snapshot_.rdsAfCount = rds.afCount;
        for (uint8_t af = 0; af < rds.afCount; ++af) snapshot_.rdsAfKHz[af] = rds.afKHz[af];
      }
      if (rds.eonUpdated) {
        snapshot_.rdsEonValid = rds.eon.valid;
        snapshot_.rdsEonPi = rds.eon.pi;
        snapshot_.rdsEonTp = rds.eon.tp;
        snapshot_.rdsEonTa = rds.eon.ta;
        snapshot_.rdsEonPty = rds.eon.pty;
        snapshot_.rdsEonAfCount = rds.eon.afCount;
        for (uint8_t af = 0; af < rds.eon.afCount; ++af)
          snapshot_.rdsEonAfKHz[af] = rds.eon.afKHz[af];
        copyField(snapshot_.rdsEonPs, sizeof(snapshot_.rdsEonPs), rds.eon.ps);
      }
      if (rds.odaUpdated) {
        snapshot_.rdsOdaAid = rds.odaAid;
        snapshot_.rdsOdaGroupType = rds.odaGroupType;
        snapshot_.rdsOdaGroupVersion = rds.odaGroupVersion;
      }
      if (rds.rtPlusUpdated) {
        snapshot_.rdsRtPlusItemToggle = rds.rtPlusItemToggle;
        snapshot_.rdsRtPlusItemRunning = rds.rtPlusItemRunning;
        for (uint8_t tag = 0; tag < 2; ++tag) {
          snapshot_.rdsRtPlusValid[tag] = rds.rtPlus[tag].valid;
          snapshot_.rdsRtPlusType[tag] = rds.rtPlus[tag].contentType;
          snapshot_.rdsRtPlusStart[tag] = rds.rtPlus[tag].start;
          snapshot_.rdsRtPlusLength[tag] = rds.rtPlus[tag].length;
          copyField(snapshot_.rdsRtPlusText[tag], sizeof(snapshot_.rdsRtPlusText[tag]),
                    rds.rtPlus[tag].text);
        }
      }
      if (rds.clockValid) {
        snapshot_.rdsClockHour = rds.clockHour;
        snapshot_.rdsClockMinute = rds.clockMinute;
        snapshot_.rdsClockValid = true;
      }
      if (rds.stationNameUpdated) copyField(snapshot_.stationName, sizeof(snapshot_.stationName), rds.stationName);
      if (rds.radioTextUpdated)
        copyField(snapshot_.radioText, sizeof(snapshot_.radioText), rds.radioText);
      snapshot_.rdsAvailable = true;
    }
  }
}

bool RadioCore::tune(uint32_t frequencyKHz) {
  const auto& band = BandManager::current();
  if (!ready_ || !BandManager::contains(BandManager::currentIndex(), frequencyKHz) ||
      ((snapshot_.modulation == RadioModulation::LSB || snapshot_.modulation == RadioModulation::USB) &&
       (frequencyKHz < 1800 || frequencyKHz > 30000)) ||
      !tuner_.tune(frequencyKHz, band)) return false;
  snapshot_.frequencyKHz = tuner_.frequencyKHz();
  clearRdsSnapshot(snapshot_);
  snapshot_.generation = ++generation_;
  return true;
}

bool RadioCore::selectBand(uint8_t index) {
  if (!ready_ || index >= BandManager::count()) return false;
  const auto& band = BandManager::at(index);
  if (!tuner_.setBand(band) || !BandManager::select(index)) return false;
  publishBand(snapshot_, band);
  snapshot_.frequencyKHz = tuner_.frequencyKHz();
  clearRdsSnapshot(snapshot_);
  snapshot_.generation = ++generation_;
  lastRdsPollMs_ = millis();
  Serial.printf("[RADIO] band=%s mode=%u freq=%lu kHz\n",
                band.name, static_cast<unsigned>(band.modulation),
                static_cast<unsigned long>(snapshot_.frequencyKHz));
  return true;
}

bool RadioCore::canUseModulation(RadioModulation modulation) const {
  const auto& band = BandManager::current();
  if (band.type == RadioBandType::FM) return modulation == RadioModulation::FM;
  if (modulation == RadioModulation::FM) return false;
  if (modulation == RadioModulation::AM) return true;
  return snapshot_.frequencyKHz >= 1800 && snapshot_.frequencyKHz <= 30000 &&
         (modulation == RadioModulation::LSB || modulation == RadioModulation::USB);
}
bool RadioCore::setModulation(RadioModulation modulation) {
  if (!ready_ || !canUseModulation(modulation)) return false;
  if (snapshot_.modulation == modulation) return true;
  const auto& band = BandManager::current();
  if (!tuner_.setModulation(band, modulation)) return false;
  BandManager::setCurrentStepKHz(band.stepKHz ? band.stepKHz : 1);
  snapshot_.modulation = modulation;
  snapshot_.frequencyKHz = tuner_.frequencyKHz();
  clearRdsSnapshot(snapshot_);
  snapshot_.stereo = false;
  snapshot_.generation = ++generation_;
  lastRdsPollMs_ = millis();
  Serial.printf("[RADIO] modulation=%u freq=%lu kHz band=%s\n",
    static_cast<unsigned>(modulation), static_cast<unsigned long>(snapshot_.frequencyKHz), snapshot_.bandName);
  return true;
}
bool RadioCore::nextBand(int8_t direction) {
  if (!direction) return false;
  int16_t index = static_cast<int16_t>(BandManager::currentIndex()) + (direction > 0 ? 1 : -1);
  if (index < 0) index = BandManager::count() - 1;
  if (index >= BandManager::count()) index = 0;
  return selectBand(static_cast<uint8_t>(index));
}

bool RadioCore::setVolume(uint8_t volume) {
  return ready_ && tuner_.setVolume(volume);
}

bool RadioCore::setBandwidthChoice(uint8_t choice) {
  if (!ready_ || !tuner_.setBandwidthChoice(choice)) return false;
  snapshot_.generation = ++generation_;
  return true;
}

bool RadioCore::setStepChoice(uint8_t choice) {
  static const uint16_t fmSteps[] = {10, 50, 100, 200, 1000};
  static const uint16_t amSteps[] = {1, 5, 9, 10};
  if (!ready_ || choice >= (snapshot_.modulation == RadioModulation::FM ? 5 : 4)) return false;
  const uint16_t step = snapshot_.modulation == RadioModulation::FM ? fmSteps[choice] : amSteps[choice];
  if (!tuner_.setStepKHz(step) || !BandManager::setCurrentStepKHz(step)) return false;
  snapshot_.generation = ++generation_;
  Serial.printf("[RADIO] step=%u kHz mode=%u\n", static_cast<unsigned>(step), static_cast<unsigned>(snapshot_.modulation));
  return true;
}

bool RadioCore::nextChannel(int16_t acceleratedDelta) {
  if (!ready_ || acceleratedDelta == 0) return false;
  const auto& band = BandManager::current();
  const uint32_t step = BandManager::currentStepKHz() ? BandManager::currentStepKHz() : (band.stepKHz ? band.stepKHz : 1);
  uint32_t minimum = band.minimumKHz, maximum = band.maximumKHz;
  if (snapshot_.modulation == RadioModulation::LSB || snapshot_.modulation == RadioModulation::USB) {
    if (minimum < 1800) minimum = 1800;
    if (maximum > 30000) maximum = 30000;
  }
  const int64_t span = static_cast<int64_t>(maximum) - minimum + step;
  int64_t offset = static_cast<int64_t>(snapshot_.frequencyKHz) - minimum;
  offset += static_cast<int64_t>(acceleratedDelta) * step;
  offset %= span;
  if (offset < 0) offset += span;
  const uint32_t targetKHz = minimum + static_cast<uint32_t>(offset);

  const bool tuned = tune(targetKHz);
  return tuned;
}

bool RadioCore::setAgcAutomatic(bool automatic) { return ready_ && tuner_.setAgcAutomatic(automatic); }
bool RadioCore::setAgcManualGain(uint8_t index) { return ready_ && tuner_.setAgcManualGain(index); }
bool RadioCore::setAttenuationIndex(uint8_t index) { return ready_ && tuner_.setAttenuationIndex(index); }
bool RadioCore::setSquelch(uint8_t threshold, bool useSnr) { return ready_ && tuner_.setSquelch(threshold, useSnr); }
bool RadioCore::setFmRegion(uint8_t region) {
  if (!ready_ || !tuner_.setFmRegion(region)) return false;
  snapshot_.frequencyKHz = tuner_.frequencyKHz();
  snapshot_.bandMinimumKHz = region == 2 ? 76000 : (region == 1 ? 87900 : 87500);
  snapshot_.bandMaximumKHz = region == 2 ? 95000 : (region == 1 ? 107900 : 108000);
  snapshot_.tuningStepKHz = region == 1 ? 200 : 100;
  snapshot_.generation = ++generation_;
  return true;
}
bool RadioCore::setFmStereoMode(uint8_t mode) { return ready_ && tuner_.setFmStereoMode(mode); }
bool RadioCore::setSoftMuteEnabled(bool enabled) { return ready_ && tuner_.setSoftMuteEnabled(enabled); }
bool RadioCore::setSoftMuteAttenuation(uint8_t attenuationDb) { return ready_ && tuner_.setSoftMuteAttenuation(attenuationDb); }
bool RadioCore::setAvcGain(uint8_t gain) { return ready_ && tuner_.setAvcGain(gain); }
bool RadioCore::setPatchProfile(uint8_t profile) {
  if (!ready_ || profile > 2 || !tuner_.setPatchProfile(profile)) return false;
  RadioBandDefinition configured = BandManager::current();
  configured.modulation = snapshot_.modulation;
  configured.startKHz = snapshot_.frequencyKHz;
  if (!tuner_.setBand(configured)) return false;
  snapshot_.frequencyKHz = tuner_.frequencyKHz();
  snapshot_.generation = ++generation_;
  Serial.printf("[RADIO] DSP patch profile=%u\n", static_cast<unsigned>(profile));
  return true;
}

bool RadioCore::setBfoOffsetHz(int16_t offsetHz) {
  if (!ready_ || !tuner_.setBfoOffsetHz(offsetHz)) return false;
  snapshot_.bfoOffsetHz = tuner_.bfoOffsetHz();
  snapshot_.generation = ++generation_;
  return true;
}
bool RadioCore::setSsbBandwidthChoice(uint8_t choice) { return tuner_.setSsbBandwidthChoice(choice); }
