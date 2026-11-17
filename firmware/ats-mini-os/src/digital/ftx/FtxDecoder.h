#pragma once

#include <Arduino.h>
#include <time.h>
#include "ft8_lib/common/monitor.h"

enum class FtxMode : uint8_t { FT8 = 0, FT4 = 1 };

struct FtxDecodedMessage {
  char text[FTX_MAX_MESSAGE_LENGTH + 1] = {};
  float audioFrequencyHz = 0.0f;
  float snrDb = -30.0f;
  uint16_t syncScore = 0;
  int ldpcErrors = 0;
  time_t slotUtc = 0;
};

// DSP/slot engine only. It never accesses GPIO, the ADC, Wi-Fi or the display.
// The caller supplies the existing 4 kHz GPIO11 sample stream and starts each
// receive slot at its UTC boundary.
class FtxDecoder {
public:
  static constexpr uint32_t kSampleRate = 4000;
  static constexpr size_t kMaxResults = 8;

  bool begin(FtxMode mode, float minAudioHz = 250.0f, float maxAudioHz = 1750.0f);
  void end();
  bool startSlot(time_t utcSlotStart);
  void pushSamples(const int16_t* samples, size_t count);
  bool slotReady() const { return slotReady_; }
  size_t decodeSlot();
  size_t resultCount() const { return resultCount_; }
  const FtxDecodedMessage& result(size_t index) const { return results_[index < resultCount_ ? index : 0]; }
  uint32_t samplesPerSymbol() const { return static_cast<uint32_t>(monitor_.block_size); }
  uint32_t symbolsCollected() const { return static_cast<uint32_t>(monitor_.wf.num_blocks); }
  float maxMagnitudeDb() const { return monitor_.max_mag; }
  size_t pendingFrameSamples() const { return frameFill_; }
  int waterfallBlocks() const { return monitor_.wf.num_blocks; }
  int waterfallMaxBlocks() const { return monitor_.wf.max_blocks; }
  int fftBlockSize() const { return monitor_.block_size; }
  bool initialized() const { return initialized_; }
  bool collecting() const { return collecting_; }
  bool copyWaterfall(uint8_t* out, uint16_t maxRows, uint16_t outBins, uint16_t& rows, uint16_t& bins) const;

private:
  static bool lookupHash(ftx_callsign_hash_type_t, uint32_t, char*);
  static void saveHash(const char*, uint32_t);

  monitor_t monitor_{};
  FtxDecodedMessage results_[kMaxResults]{};
  FtxMode mode_ = FtxMode::FT8;
  float minAudioHz_ = 250.0f;
  float dcEstimate_ = 2048.0f;
  float frame_[640]{};
  size_t frameFill_ = 0;
  size_t resultCount_ = 0;
  time_t slotUtc_ = 0;
  bool initialized_ = false;
  bool collecting_ = false;
  bool slotReady_ = false;
};
