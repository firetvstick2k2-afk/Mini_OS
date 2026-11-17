#pragma once

#include <Arduino.h>
#include <Preferences.h>

// Persistent settings owned by this module. Uses the same dedicated NVS
// partition ("settings") as ats-mini-dev. The namespace remains unique to
// ATS Mini OS so its keys do not collide with the reference firmware.
class SettingsStore {
public:
  bool begin();
  void end();

  uint32_t lastFmFrequencyKHz(uint32_t fallbackKHz);
  bool saveLastFmFrequencyKHz(uint32_t frequencyKHz);
  uint8_t volume(uint8_t fallback = 32);
  bool saveVolume(uint8_t volume);

  uint8_t displayBrightness(uint8_t fallback = 180);
  bool saveDisplayBrightness(uint8_t brightness);
  uint8_t displayTheme(uint8_t fallback = 0);
  bool saveDisplayTheme(uint8_t theme);
  uint8_t displayFmLayout(uint8_t fallback = 0);
  bool saveDisplayFmLayout(uint8_t layout);
  uint8_t displayAmLayout(uint8_t fallback = 9);
  bool saveDisplayAmLayout(uint8_t layout);
  bool agcAutomatic(bool fallback = true);
  bool saveAgcAutomatic(bool automatic);
  uint8_t agcGainIndex(uint8_t fallback = 20);
  bool saveAgcGainIndex(uint8_t index);
  uint8_t softMuteAttenuationDb(uint8_t fallback = 8);
  bool saveSoftMuteAttenuationDb(uint8_t attenuationDb);
  uint8_t avcGain(uint8_t fallback = 90);
  bool saveAvcGain(uint8_t gain);
  uint8_t attenuationIndex(uint8_t fallback = 0);
  bool saveAttenuationIndex(uint8_t index);
  uint8_t squelchThreshold(uint8_t fallback = 0);
  bool saveSquelchThreshold(uint8_t threshold);
  bool squelchUsesSnr(bool fallback = false);
  bool saveSquelchUsesSnr(bool enabled);
  uint8_t fmRegion(uint8_t fallback = 0);
  bool saveFmRegion(uint8_t region);
  uint8_t fmStereoMode(uint8_t fallback = 0);
  bool saveFmStereoMode(uint8_t mode);
  uint8_t ssbBandwidthChoice(uint8_t fallback = 4);
  bool saveSsbBandwidthChoice(uint8_t choice);
  uint8_t patchProfile(uint8_t fallback = 0);
  bool savePatchProfile(uint8_t profile);
  int16_t bfoOffsetHz(int16_t fallback = 0);
  bool saveBfoOffsetHz(int16_t offsetHz);
  uint8_t cpuProfile(uint8_t fallback = 0);
  bool saveCpuProfile(uint8_t profile);
  bool bootLogEnabled(bool fallback = true);
  bool saveBootLogEnabled(bool enabled);
  bool isReady() const { return ready_; }

private:
  static constexpr char kNamespace[] = "atsmini_os";
  static constexpr char kFrequencyKey[] = "fm_freq";
  static constexpr char kVolumeKey[] = "rx_volume";
  static constexpr char kDisplayBrightnessKey[] = "disp_bri";
  static constexpr char kDisplayThemeKey[] = "disp_theme";
  static constexpr char kDisplayFmLayoutKey[] = "disp_fm";
  static constexpr char kDisplayAmLayoutKey[] = "disp_am";
  static constexpr char kDisplayAmLayoutMigratedKey[] = "am_wf_v1";
  static constexpr char kAgcAutomaticKey[] = "rx_agc_auto";
  static constexpr char kAgcGainIndexKey[] = "rx_agc_idx";
  static constexpr char kSoftMuteKey[] = "rx_softmute";
  static constexpr char kAvcGainKey[] = "rx_avc";
  static constexpr char kAttenuationKey[] = "rx_att";
  static constexpr char kSquelchKey[] = "rx_sql";
  static constexpr char kSquelchSnrKey[] = "rx_sql_snr";
  static constexpr char kFmRegionKey[] = "fm_region";
  static constexpr char kFmStereoKey[] = "fm_stereo";
  static constexpr char kSsbBandwidthKey[] = "ssb_bw";
  static constexpr char kBfoOffsetKey[] = "ssb_bfo";
  static constexpr char kPatchProfileKey[] = "dsp_patch";
  static constexpr char kCpuProfileKey[] = "cpu_prof";
  static constexpr char kBootLogEnabledKey[] = "boot_log";

  Preferences preferences_;
  bool ready_ = false;
};
