#include "RdsDecoder.h"
#include <string.h>

namespace {
uint32_t afFrequency(uint8_t code) {
  return code >= 1 && code <= 204 ? 87500UL + static_cast<uint32_t>(code) * 100UL : 0;
}
RdsEonNetwork* findEon(RdsEonNetwork* entries, uint16_t pi) {
  RdsEonNetwork* empty = nullptr;
  for (uint8_t i = 0; i < 8; ++i) {
    if (entries[i].valid && entries[i].pi == pi) return &entries[i];
    if (!entries[i].valid && !empty) empty = &entries[i];
  }
  return empty;
}
void addEonAf(RdsEonNetwork& network, uint8_t code) {
  const uint32_t f = afFrequency(code);
  if (!f || network.afCount >= 25) return;
  for (uint8_t i = 0; i < network.afCount; ++i) if (network.afKHz[i] == f) return;
  network.afKHz[network.afCount++] = f;
}
}

void RdsDecoder::reset() {
  afCount_ = expectedAfCount_ = 0;
  odaGroupType_ = odaGroupVersion_ = 0xFF;
  rtPlusAid_ = 0;
  memset(odaAssignments_, 0, sizeof(odaAssignments_));
  memset(ptyn_, 0, sizeof(ptyn_));
  ptynSegments_ = 0;
  ptynFlag_ = 0xFF;
  memset(afKHz_, 0, sizeof(afKHz_));
  memset(eonNetworks_, 0, sizeof(eonNetworks_));
}

void RdsDecoder::addAlternativeFrequency(uint8_t code) {
  if (code >= 224 && code <= 249) {
    const uint8_t count = static_cast<uint8_t>(code - 224);
    if (count > 0 && count <= 25) expectedAfCount_ = count;
    return;
  }
  const uint32_t frequency = afFrequency(code);
  if (!frequency || afCount_ >= 25) return;
  for (uint8_t i = 0; i < afCount_; ++i) if (afKHz_[i] == frequency) return;
  afKHz_[afCount_++] = frequency;
}

void RdsDecoder::processEon(uint8_t version, uint16_t blockB, uint16_t blockC,
                            uint16_t blockD, uint8_t blockCError, uint8_t blockDError,
                            RdsDecodedFields& out) {
  // Block D carries the other-network PI in both 14A and 14B.
  if (blockDError >= 3) return;
  if (version == 0 && blockCError >= 3) return;
  const uint16_t otherPi = blockD;
  if (!otherPi) return;
  RdsEonNetwork* net = findEon(eonNetworks_, otherPi);
  if (!net) return;
  if (!net->valid) { *net = RdsEonNetwork{}; net->valid = true; net->pi = otherPi; }
  net->tp = (blockB & 0x0010U) != 0;
  if (version == 1) {
    net->ta = (blockB & 0x0008U) != 0;
    out.eon = *net;
    out.eonUpdated = true;
    return;
  }
  const uint8_t variant = static_cast<uint8_t>(blockB & 0x000FU);
  switch (variant) {
    case 0: case 1: case 2: case 3: {
      const uint8_t offset = static_cast<uint8_t>(variant * 2U);
      net->ps[offset] = static_cast<char>(blockC >> 8);
      net->ps[offset + 1] = static_cast<char>(blockC & 0xFF);
      net->ps[8] = '\0';
      break;
    }
    case 4:
      addEonAf(*net, static_cast<uint8_t>(blockC >> 8));
      addEonAf(*net, static_cast<uint8_t>(blockC));
      break;
    case 13:
      net->pty = static_cast<uint8_t>((blockC >> 11) & 0x1FU);
      net->ta = (blockC & 0x0001U) != 0;
      break;
    default:
      // Other variant payloads (mapped frequencies, linkage and PIN) are
      // deliberately not reinterpreted as generic data.
      break;
  }
  out.eon = *net;
  out.eonUpdated = true;
}

void RdsDecoder::processPtyn(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                            uint8_t blockCError, uint8_t blockDError,
                            RdsDecodedFields& out) {
  if (blockCError >= 3 || blockDError >= 3) return;
  const uint8_t flag = static_cast<uint8_t>((blockB >> 4) & 0x01U);
  if (ptynFlag_ != flag) {
    memset(ptyn_, 0, sizeof(ptyn_));
    ptynSegments_ = 0;
    ptynFlag_ = flag;
  }
  const uint8_t segment = static_cast<uint8_t>(blockB & 0x03U);
  const uint8_t offset = static_cast<uint8_t>(segment * 4U);
  const uint8_t bytes[4] = { static_cast<uint8_t>(blockC >> 8),
    static_cast<uint8_t>(blockC), static_cast<uint8_t>(blockD >> 8),
    static_cast<uint8_t>(blockD) };
  for (uint8_t i = 0; i < 4; ++i) {
    const uint8_t ch = bytes[i];
    ptyn_[offset + i] = (ch >= 0x20U && ch <= 0x7EU) ? static_cast<char>(ch) : ' ';
  }
  ptyn_[8] = '\0';
  ptynSegments_ |= static_cast<uint8_t>(1U << segment);
  if (ptynSegments_ == 0x03U) {
    memcpy(out.ptyn, ptyn_, sizeof(out.ptyn));
    out.ptynUpdated = true;
  }
}
void RdsDecoder::processOda(uint16_t blockC, uint16_t blockD,
                            uint8_t blockCError, uint8_t blockDError,
                            RdsDecodedFields& out) {
  if (blockCError >= 3 || blockDError >= 3) return;
  // IEC 62106-2:2021: 3A block C carries the 5-bit Application Group Type
  // Code (A3..A0 plus B0); block D carries the AID. It is not in block B.
  const uint8_t applicationGroupCode = static_cast<uint8_t>((blockC >> 11) & 0x1FU);
  const uint8_t groupType = static_cast<uint8_t>((applicationGroupCode >> 1) & 0x0FU);
  const uint8_t groupVersion = static_cast<uint8_t>(applicationGroupCode & 0x01U);
  out.odaAid = blockD;
  out.odaGroupType = groupType;
  out.odaGroupVersion = groupVersion;
  out.odaUpdated = true;
  // A zero AGTC means the application group is not currently carried;
  // 11111 signals a temporary data fault. Neither may activate RT+.
  OdaAssignment* assignment = nullptr;
  OdaAssignment* empty = nullptr;
  for (uint8_t i = 0; i < 8; ++i) {
    if (odaAssignments_[i].valid && odaAssignments_[i].aid == blockD) {
      assignment = &odaAssignments_[i];
      break;
    }
    if (!odaAssignments_[i].valid && !empty) empty = &odaAssignments_[i];
  }
  if (applicationGroupCode == 0 || applicationGroupCode == 0x1F) {
    if (assignment) *assignment = OdaAssignment{};
  } else {
    if (!assignment) assignment = empty;
    if (assignment) {
      assignment->valid = true;
      assignment->aid = blockD;
      assignment->groupType = groupType;
      assignment->groupVersion = groupVersion;
    }
  }
  // Keep the RT+ dispatch assignment independent from unrelated ODAs. A
  // station may announce several applications in its 3A groups.
  rtPlusAid_ = 0;
  odaGroupType_ = odaGroupVersion_ = 0xFF;
  for (uint8_t i = 0; i < 8; ++i) {
    if (odaAssignments_[i].valid && odaAssignments_[i].aid == 0x4BD7U) {
      rtPlusAid_ = 0x4BD7U;
      odaGroupType_ = odaAssignments_[i].groupType;
      odaGroupVersion_ = odaAssignments_[i].groupVersion;
      break;
    }
  }
}

void RdsDecoder::processRtPlus(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                               const char* radioText, uint8_t blockCError, uint8_t blockDError,
                               RdsDecodedFields& out) {
  if (!radioText || rtPlusAid_ != 0x4BD7U ||
      blockCError >= 3 || blockDError >= 3) return;
  // The 37 RT+ message bits are contiguous: B[4:0], C[15:0], D[15:0].
  const uint64_t bits = (static_cast<uint64_t>(blockB & 0x001FU) << 32) |
                        (static_cast<uint64_t>(blockC) << 16) | blockD;
  out.rtPlusItemToggle = ((bits >> 36) & 1U) != 0;
  out.rtPlusItemRunning = ((bits >> 35) & 1U) != 0;
  const uint8_t types[2] = {
    static_cast<uint8_t>((bits >> 29) & 0x3FU),
    static_cast<uint8_t>((bits >> 11) & 0x3FU)
  };
  const uint8_t starts[2] = {
    static_cast<uint8_t>((bits >> 23) & 0x3FU),
    static_cast<uint8_t>((bits >> 5) & 0x3FU)
  };
  const uint8_t lengths[2] = {
    static_cast<uint8_t>((bits >> 17) & 0x3FU),
    static_cast<uint8_t>(bits & 0x1FU)
  };
  for (uint8_t i = 0; i < 2; ++i) {
    RdsRtPlusTag& tag = out.rtPlus[i];
    tag.contentType = types[i];
    tag.start = starts[i];
    tag.length = lengths[i];
    // Length marker encodes the number of additional characters after start.
    const uint16_t count = static_cast<uint16_t>(lengths[i]) + 1U;
    if (types[i] == 0 || starts[i] >= 64 || starts[i] + count > 64) continue;
    for (uint16_t n = 0; n < count && n < sizeof(tag.text) - 1; ++n) {
      const char ch = radioText[starts[i] + n];
      if (!ch || ch == '\r' || ch == '\n') break;
      tag.text[n] = (ch >= 0x20 && ch <= 0x7E) ? ch : ' ';
    }
    tag.valid = tag.text[0] != '\0';
  }
  out.rtPlusUpdated = true;
}

void RdsDecoder::process(uint8_t groupType, uint8_t version, uint16_t blockB,
                         uint16_t blockC, uint16_t blockD, uint16_t pi,
                         uint8_t blockCError, uint8_t blockDError, const char* radioText,
                         RdsDecodedFields& out) {
  out.tpValid = true;
  out.tp = (blockB & 0x0400U) != 0;
  out.pty = static_cast<uint8_t>((blockB >> 5) & 0x1FU);
  if (groupType == 0) {
    out.taValid = out.msValid = out.diValid = true;
    out.ta = (blockB & 0x0010U) != 0;
    out.musicSpeech = (blockB & 0x0008U) != 0;
    out.decoderIdentification = (blockB & 0x0004U) != 0;
  }
  if (groupType == 1 && version == 0 && blockCError < 3) {
    const uint8_t ecc = static_cast<uint8_t>(blockC >> 8);
    if (ecc) { out.eccValid = true; out.ecc = ecc; }
  }
  if (groupType == 0 && version == 0 && blockCError < 3) {
    addAlternativeFrequency(static_cast<uint8_t>(blockC >> 8));
    addAlternativeFrequency(static_cast<uint8_t>(blockC));
    out.afUpdated = true;
    out.afCount = afCount_;
    for (uint8_t i = 0; i < afCount_; ++i) out.afKHz[i] = afKHz_[i];
  }
  if (groupType == 10 && version == 0)
    processPtyn(blockB, blockC, blockD, blockCError, blockDError, out);
  if (groupType == 14)
    processEon(version, blockB, blockC, blockD, blockCError, blockDError, out);
  if (groupType == 3 && version == 0)
    processOda(blockC, blockD, blockCError, blockDError, out);
  if (rtPlusAid_ == 0x4BD7U && groupType == odaGroupType_ &&
      version == odaGroupVersion_ && version == 0)
    processRtPlus(blockB, blockC, blockD, radioText,
                  blockCError, blockDError, out);
  (void)pi;
}
