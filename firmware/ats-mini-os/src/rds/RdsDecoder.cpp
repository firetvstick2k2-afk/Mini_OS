#include "RdsDecoder.h"

void RdsDecoder::reset() {
  afCount_ = 0;
  expectedAfCount_ = 0;
  for (uint8_t i = 0; i < 25; ++i) afKHz_[i] = 0;
}

void RdsDecoder::addAlternativeFrequency(uint8_t code) {
  // RDS AF codes 1..204 represent 87.6..107.9 MHz in 100 kHz steps.
  if (code >= 224 && code <= 249) {
    const uint8_t count = static_cast<uint8_t>(code - 224);
    if (count > 0 && count <= 25) expectedAfCount_ = count;
    return;
  }
  if (code < 1 || code > 204 || afCount_ >= 25) return;
  const uint32_t frequency = 87500UL + static_cast<uint32_t>(code) * 100UL;
  for (uint8_t i = 0; i < afCount_; ++i) if (afKHz_[i] == frequency) return;
  afKHz_[afCount_++] = frequency;
}

void RdsDecoder::process(uint8_t groupType, uint8_t version, uint16_t blockB,
                         uint16_t blockC, uint8_t blockCError,
                         RdsDecodedFields& out) {
  // Block B common fields: TP[10], PTY[9:5], TA[4], MS[3], DI[2].
  out.tpValid = true;
  out.tp = (blockB & 0x0400U) != 0;
  out.pty = static_cast<uint8_t>((blockB >> 5) & 0x1FU);
  out.taValid = groupType == 0;
  if (out.taValid) {
    out.ta = (blockB & 0x0010U) != 0;
    out.msValid = true;
    out.musicSpeech = (blockB & 0x0008U) != 0;
    out.diValid = true;
    out.decoderIdentification = (blockB & 0x0004U) != 0;
  }

  // Group 1A carries ECC in the high byte of Block C. Do not mistake
  // the low-byte Linkage Information Code for ECC.
  if (groupType == 1 && version == 0 && blockCError < 3) {
    const uint8_t ecc = static_cast<uint8_t>(blockC >> 8);
    if (ecc != 0) {
      out.eccValid = true;
      out.ecc = ecc;
    }
  }

  // Alternative frequencies are carried only by version A group 0.
  if (groupType == 0 && version == 0 && blockCError < 3) {
    addAlternativeFrequency(static_cast<uint8_t>(blockC >> 8));
    addAlternativeFrequency(static_cast<uint8_t>(blockC & 0xFF));
    out.afUpdated = true;
    out.afCount = afCount_;
    for (uint8_t i = 0; i < afCount_; ++i) out.afKHz[i] = afKHz_[i];
  }
}
