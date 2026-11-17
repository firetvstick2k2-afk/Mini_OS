#include "FtxDecoder.h"

#include <math.h>
#include <string.h>

namespace {
constexpr int kCandidateCount = 20;
constexpr int kMinSyncScore = 10;
constexpr int kLdpcIterations = 25;

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
  ftx_callsign_hash_interface_t hashIf{};
  hashIf.lookup_hash = &FtxDecoder::lookupHash;
  hashIf.save_hash = &FtxDecoder::saveHash;

  for (int i = found - 1; i >= 0 && resultCount_ < kMaxResults; --i) {
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

    FtxDecodedMessage& out = results_[resultCount_++];
    strncpy(out.text, decoded, sizeof(out.text) - 1);
    const float symbolPeriod = mode_ == FtxMode::FT4 ? FT4_SYMBOL_PERIOD : FT8_SYMBOL_PERIOD;
    out.audioFrequencyHz = minAudioHz_ + candidates[i].freq_offset / symbolPeriod;
    out.syncScore = candidates[i].score > 0 ? static_cast<uint16_t>(candidates[i].score) : 0;
    out.ldpcErrors = status.ldpc_errors;
    out.slotUtc = slotUtc_;
  }

  slotReady_ = false;
  return resultCount_;
}
