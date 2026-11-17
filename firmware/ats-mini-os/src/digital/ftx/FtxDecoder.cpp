#include "FtxDecoder.h"
#include "ft8_lib/ft8/encode.h"

#include <math.h>
#include <string.h>

namespace {
constexpr int kCandidateCount = 20;
constexpr int kMinSyncScore = 10;
constexpr int kLdpcIterations = 25;
constexpr float kSnrCalibrationDb = 1.7f;

// Median waterfall-bin power, converted from the library's 0.5 dB magnitude
// encoding to linear power. The 0.6931 factor corrects the exponential noise median.
float estimateNoisePower(const ftx_waterfall_t& wf) {
  uint32_t histogram[256] = {};
  const int count = wf.num_blocks * wf.block_stride;
  if (count <= 0) return 1.0f;
  for (int i = 0; i < count; ++i)
    ++histogram[WF_ELEM_MAG_INT(wf.mag[i]) & 255];
  uint32_t accumulated = 0;
  int median = 0;
  while (median < 255 && (accumulated += histogram[median]) < static_cast<uint32_t>(count) / 2)
    ++median;
  return powf(10.0f, (static_cast<float>(median) * 0.5f - 120.0f) / 10.0f) / 0.6931f;
}

float estimateSnrDb(const monitor_t& monitor, const ftx_candidate_t& candidate,
                    const uint8_t* tones, float noisePower) {
  const ftx_waterfall_t& wf = monitor.wf;
  const bool ft4 = wf.protocol == FTX_PROTOCOL_FT4;
  const int symbolCount = ft4 ? FT4_NN : FT8_NN;
  const int toneCount = ft4 ? 4 : 8;
  const WF_ELEM_T* base = wf.mag + candidate.time_sub * wf.freq_osr * wf.num_bins +
    candidate.freq_sub * wf.num_bins + candidate.freq_offset;
  float signalPower = 0.0f;
  int signalSymbols = 0;
  for (int symbol = ft4 ? 1 : 0; symbol < (ft4 ? symbolCount - 1 : symbolCount); ++symbol) {
    const int block = candidate.time_offset + symbol;
    if (block < 0 || block >= wf.num_blocks) continue;
    const WF_ELEM_T* row = base + block * wf.block_stride;
    const int tone = tones[symbol];
    if (tone < toneCount && candidate.freq_offset + tone < wf.num_bins) {
      signalPower += powf(10.0f, WF_ELEM_MAG(row[tone]) / 10.0f);
      ++signalSymbols;
    }
  }
  if (!signalSymbols) return -30.0f;
  const float meanSignalPower = signalPower / signalSymbols;
  const float equivalentNoiseBandwidth = 1.5f / (monitor.symbol_period * wf.freq_osr);
  const float signalAboveNoise = meanSignalPower - noisePower > 1e-3f * noisePower
    ? meanSignalPower - noisePower : 1e-3f * noisePower;
  return 10.0f * log10f(signalAboveNoise / noisePower *
    equivalentNoiseBandwidth / 2500.0f) + kSnrCalibrationDb;
}
}

bool FtxDecoder::lookupHash(ftx_callsign_hash_type_t, uint32_t, char*) {
  // No persistent callsign hash database is installed yet. The library will
  // retain its normal unknown-hash representation rather than inventing a call.
  return false;
}

void FtxDecoder::saveHash(const char*, uint32_t) {
  // A compact persistent hash cache can be added independently later.
}

bool FtxDecoder::begin(FtxMode mode, float minAudioHz, float maxAudioHz) {
  end();
  if (minAudioHz < 50.0f || maxAudioHz <= minAudioHz ||
      maxAudioHz >= (kSampleRate * 0.5f)) return false;

  mode_ = mode;
  minAudioHz_ = minAudioHz;
  monitor_config_t cfg{};
  cfg.f_min = minAudioHz;
  cfg.f_max = maxAudioHz;
  cfg.sample_rate = static_cast<int>(kSampleRate);
  cfg.time_osr = 1;
  cfg.freq_osr = 1;
  cfg.protocol = mode == FtxMode::FT4 ? FTX_PROTOCOL_FT4 : FTX_PROTOCOL_FT8;

  monitor_ = {};
  monitor_init(&monitor_, &cfg);
  if (!monitor_.window || !monitor_.last_frame || !monitor_.timedata ||
      !monitor_.freqdata || !monitor_.fft_work || !monitor_.fft_cfg ||
      !monitor_.wf.mag || monitor_.block_size <= 0 ||
      monitor_.block_size > static_cast<int>(sizeof(frame_) / sizeof(frame_[0]))) {
    monitor_free(&monitor_);
    monitor_ = {};
    return false;
  }

  dcEstimate_ = 2048.0f;
  frameFill_ = resultCount_ = 0;
  slotUtc_ = 0;
  collecting_ = slotReady_ = false;
  initialized_ = true;
  return true;
}

void FtxDecoder::end() {
  if (initialized_) monitor_free(&monitor_);
  monitor_ = {};
  initialized_ = collecting_ = slotReady_ = false;
  frameFill_ = resultCount_ = 0;
}

bool FtxDecoder::startSlot(time_t utcSlotStart) {
  if (!initialized_ || utcSlotStart <= 0) return false;
  monitor_reset(&monitor_);
  memset(frame_, 0, sizeof(frame_));
  memset(results_, 0, sizeof(results_));
  frameFill_ = resultCount_ = 0;
  slotUtc_ = utcSlotStart;
  collecting_ = true;
  slotReady_ = false;
  return true;
}

void FtxDecoder::pushSamples(const int16_t* samples, size_t count) {
  if (!collecting_ || !samples) return;
  for (size_t i = 0; i < count && collecting_; ++i) {
    const float raw = static_cast<float>(samples[i]);
    dcEstimate_ += (raw - dcEstimate_) * 0.001f;
    float audio = (raw - dcEstimate_) / 2048.0f;
    if (audio > 1.0f) audio = 1.0f;
    if (audio < -1.0f) audio = -1.0f;
    frame_[frameFill_++] = audio;

    if (frameFill_ >= static_cast<size_t>(monitor_.block_size)) {
      monitor_process(&monitor_, frame_);
      frameFill_ = 0;
      if (monitor_.wf.num_blocks >= monitor_.wf.max_blocks) {
        collecting_ = false;
        slotReady_ = true;
      }
    }
  }
}

size_t FtxDecoder::decodeSlot() {
  if (!slotReady_ || !initialized_) return resultCount_;

  ftx_candidate_t candidates[kCandidateCount]{};
  const int found = ftx_find_candidates(&monitor_.wf, kCandidateCount,
                                        candidates, kMinSyncScore);
  const float noisePower = found ? estimateNoisePower(monitor_.wf) : 1.0f;
  ftx_callsign_hash_interface_t hashIf{};
  hashIf.lookup_hash = &FtxDecoder::lookupHash;
  hashIf.save_hash = &FtxDecoder::saveHash;

  for (int i = 0; i < found && resultCount_ < kMaxResults; ++i) {
    ftx_message_t message{};
    ftx_decode_status_t status{};
    if (!ftx_decode_candidate(&monitor_.wf, &candidates[i],
                              kLdpcIterations, &message, &status)) continue;

    char decoded[FTX_MAX_MESSAGE_LENGTH + 1] = {};
    ftx_message_offsets_t offsets{};
    if (ftx_message_decode(&message, &hashIf, decoded, &offsets) != FTX_MESSAGE_RC_OK ||
        decoded[0] == '\0') continue;

    bool duplicate = false;
    for (size_t j = 0; j < resultCount_; ++j) {
      if (strcmp(results_[j].text, decoded) == 0) { duplicate = true; break; }
    }
    if (duplicate) continue;

    uint8_t tones[FT4_NN > FT8_NN ? FT4_NN : FT8_NN] = {};
    if (mode_ == FtxMode::FT4) ft4_encode(message.payload, tones);
    else ft8_encode(message.payload, tones);

    FtxDecodedMessage& out = results_[resultCount_++];
    strncpy(out.text, decoded, sizeof(out.text) - 1);
    out.snrDb = estimateSnrDb(monitor_, candidates[i], tones, noisePower);
    const float symbolPeriod = mode_ == FtxMode::FT4 ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
    out.audioFrequencyHz = minAudioHz_ + candidates[i].freq_offset / symbolPeriod;
    out.syncScore = candidates[i].score > 0 ? static_cast<uint16_t>(candidates[i].score) : 0;
    out.ldpcErrors = status.ldpc_errors;
    out.slotUtc = slotUtc_;
  }

  slotReady_ = false;
  return resultCount_;
}

bool FtxDecoder::copyWaterfall(uint8_t* out, uint16_t maxRows, uint16_t outBins,
                               uint16_t& rows, uint16_t& bins) const {
  rows = bins = 0;
  if (!initialized_ || !out || !maxRows || !outBins || !monitor_.wf.mag ||
      monitor_.wf.num_blocks <= 0 || monitor_.wf.num_bins <= 0) return false;

  rows = static_cast<uint16_t>(monitor_.wf.num_blocks < maxRows ?
    monitor_.wf.num_blocks : maxRows);
  bins = outBins;
  const int firstRow = monitor_.wf.num_blocks - rows;
  for (uint16_t y = 0; y < rows; ++y) {
    const int srcRow = firstRow + y;
    for (uint16_t x = 0; x < outBins; ++x) {
      int srcBin = (static_cast<int>(x) * monitor_.wf.num_bins) / outBins;
      if (srcBin >= monitor_.wf.num_bins) srcBin = monitor_.wf.num_bins - 1;
      const int srcIndex = srcRow * monitor_.wf.block_stride +
        srcBin * monitor_.wf.freq_osr;
      out[static_cast<size_t>(y) * outBins + x] =
        static_cast<uint8_t>(monitor_.wf.mag[srcIndex]);
    }
  }
  return rows > 0;
}
