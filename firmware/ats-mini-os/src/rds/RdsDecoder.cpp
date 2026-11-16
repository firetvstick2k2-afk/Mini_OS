#include "RdsDecoder.h"
#include <string.h>

namespace {
uint32_t afFrequency(uint8_t code, bool bandI = false) {
  if (bandI) return code >= 1 && code <= 240
      ? 64000UL + static_cast<uint32_t>(code) * 100UL : 0;
  return code >= 1 && code <= 204
      ? 87500UL + static_cast<uint32_t>(code) * 100UL : 0;
}

// IEC 62106-2 Table 12: ITU Regions 1 and 3, 9 kHz channel spacing.
uint32_t lfmfFrequency(uint8_t code) {
  if (code >= 1 && code <= 15)
    return 153UL + static_cast<uint32_t>(code - 1U) * 9UL;
  if (code >= 16 && code <= 135)
    return 531UL + static_cast<uint32_t>(code - 16U) * 9UL;
  return 0;
}

RdsEonNetwork* findEon(RdsEonNetwork* entries, uint16_t pi, uint8_t& index) {
  RdsEonNetwork* empty = nullptr;
  uint8_t emptyIndex = 0;
  for (uint8_t i = 0; i < RDS_MAX_EON_NETWORKS; ++i) {
    if (entries[i].valid && entries[i].pi == pi) {
      index = i;
      return &entries[i];
    }
    if (!entries[i].valid && !empty) {
      empty = &entries[i];
      emptyIndex = i;
    }
  }
  index = emptyIndex;
  return empty;
}

void setRdsChar(char& destination, uint8_t source) {
  destination = (source >= 0x20U && source <= 0x7EU)
      ? static_cast<char>(source) : ' ';
}
}

void RdsDecoder::reset() {
  afCount_ = 0;
  afState_ = afExpectedCount_ = afReceivedCount_ = afHeaderCode_ = 0;
  afPairsExpected_ = afPairsReceived_ = 0;
  afLfMfPending_ = false;
  afMethodBCount_ = 0;
  memset(afKHz_, 0, sizeof(afKHz_));
  memset(afMethodBLists_, 0, sizeof(afMethodBLists_));
  odaGroupType_ = odaGroupVersion_ = 0xFF;
  rtPlusAid_ = 0;
  rtPlusForEnhancedText_ = false;
  memset(odaAssignments_, 0, sizeof(odaAssignments_));
  memset(ptyn_, 0, sizeof(ptyn_));
  ptynSegments_ = 0;
  ptynFlag_ = 0xFF;
  memset(eonNetworks_, 0, sizeof(eonNetworks_));
}

void RdsDecoder::addAlternativeFrequency(uint32_t frequencyKHz) {
  if (!frequencyKHz || afCount_ >= 25) return;
  for (uint8_t i = 0; i < afCount_; ++i)
    if (afKHz_[i] == frequencyKHz) return;
  afKHz_[afCount_++] = frequencyKHz;
}

void RdsDecoder::addAfMethodB(uint32_t tuningKHz, uint32_t alternativeKHz) {
  if (!tuningKHz || !alternativeKHz || tuningKHz == alternativeKHz) return;
  RdsAfMethodBList* list = nullptr;
  RdsAfMethodBList* empty = nullptr;
  for (uint8_t i = 0; i < RDS_MAX_AF_METHOD_B_LISTS; ++i) {
    if (afMethodBLists_[i].valid && afMethodBLists_[i].tuningKHz == tuningKHz) {
      list = &afMethodBLists_[i];
      break;
    }
    if (!afMethodBLists_[i].valid && !empty) empty = &afMethodBLists_[i];
  }
  if (!list) {
    list = empty;
    if (!list) return;
    *list = RdsAfMethodBList{};
    list->valid = true;
    list->tuningKHz = tuningKHz;
    ++afMethodBCount_;
  }
  if (list->afCount < RDS_MAX_AF_METHOD_B_ENTRIES) {
    for (uint8_t i = 0; i < list->afCount; ++i)
      if (list->afKHz[i] == alternativeKHz) return;
    list->afKHz[list->afCount++] = alternativeKHz;
  }
  addAlternativeFrequency(alternativeKHz);
}

void RdsDecoder::processAlternativeFrequencyByte(uint8_t code) {
  if (afLfMfPending_) {
    const uint32_t frequency = lfmfFrequency(code);
    if (frequency) addAlternativeFrequency(frequency);
    afLfMfPending_ = false;
    if (afState_ == 2 && afExpectedCount_) {
      ++afReceivedCount_;
      if (afReceivedCount_ >= afExpectedCount_) afState_ = 0;
    }
    return;
  }

  if (code == 250) {
    afLfMfPending_ = true;
    return;
  }
  if (code == 205 || code == 0 || code > 204) return;
  addAlternativeFrequency(afFrequency(code));
  if (afState_ == 2 && afExpectedCount_) {
    ++afReceivedCount_;
    if (afReceivedCount_ >= afExpectedCount_) afState_ = 0;
  }
}

void RdsDecoder::publishAf(RdsDecodedFields& out) {
  out.afUpdated = true;
  out.afCount = afCount_;
  for (uint8_t i = 0; i < afCount_; ++i) out.afKHz[i] = afKHz_[i];
}

void RdsDecoder::publishAfMethodB(RdsDecodedFields& out) {
  out.afMethodBUpdated = true;
  out.afMethodBCount = afMethodBCount_;
  for (uint8_t i = 0; i < afMethodBCount_; ++i)
    out.afMethodBLists[i] = afMethodBLists_[i];
}

void RdsDecoder::processAlternativeFrequencies(uint16_t blockC, RdsDecodedFields& out) {
  const uint8_t first = static_cast<uint8_t>(blockC >> 8);
  const uint8_t second = static_cast<uint8_t>(blockC);

  // A Method-B list starts with [number of frequencies, tuning frequency].
  // Each following block contains one complete [F1,F2] pair, with the tuned
  // frequency appearing in either byte (ascending or descending order).
  if (afState_ == 3) {
    if (first == afHeaderCode_ || second == afHeaderCode_) {
      const uint8_t alternativeCode = first == afHeaderCode_ ? second : first;
      const uint32_t alternative = afFrequency(alternativeCode);
      if (alternative) {
        addAfMethodB(afFrequency(afHeaderCode_), alternative);
        ++afPairsReceived_;
        publishAfMethodB(out);
      }
      if (afPairsReceived_ >= afPairsExpected_) afState_ = 0;
      return;
    }
    // A new list header may follow the final pair. Start it without treating
    // its count/tuning-frequency bytes as ordinary AF values.
    if (first >= 225 && first <= 249 && afFrequency(second)) {
      afExpectedCount_ = static_cast<uint8_t>(first - 224U);
      afHeaderCode_ = second;
      afPairsExpected_ = afExpectedCount_ > 0
          ? static_cast<uint8_t>(afExpectedCount_ - 1U) : 0;
      afPairsReceived_ = 0;
      afState_ = afPairsExpected_ ? 1 : 0;
      return;
    }
    afState_ = 0;
    afExpectedCount_ = afReceivedCount_ = 0;
  }

  if (afState_ == 1) {
    // Method B is recognized by the tuned frequency repeating in the next
    // complete pair. Otherwise the bytes are the first Method-A AF entries.
    if (first == afHeaderCode_ || second == afHeaderCode_) {
      const uint8_t alternativeCode = first == afHeaderCode_ ? second : first;
      const uint32_t alternative = afFrequency(alternativeCode);
      afState_ = 3;
      afPairsReceived_ = 0;
      if (alternative && afPairsExpected_) {
        addAfMethodB(afFrequency(afHeaderCode_), alternative);
        ++afPairsReceived_;
        publishAfMethodB(out);
      }
      if (afPairsReceived_ >= afPairsExpected_) afState_ = 0;
      return;
    }
    afState_ = 2;
    afReceivedCount_ = 0;
    processAlternativeFrequencyByte(first);
    processAlternativeFrequencyByte(second);
    publishAf(out);
    return;
  }

  if (afState_ == 2) {
    processAlternativeFrequencyByte(first);
    processAlternativeFrequencyByte(second);
    publishAf(out);
    return;
  }

  // Method-A count header, or a Method-B list header. The second byte is
  // retained as the candidate tuning frequency until the next pair identifies
  // which AF method is in use.
  if (first == 224) {
    afCount_ = 0;
    afState_ = 0;
    afExpectedCount_ = afReceivedCount_ = 0;
    publishAf(out);
    return;
  }
  if (first >= 225 && first <= 249) {
    afExpectedCount_ = static_cast<uint8_t>(first - 224U);
    afReceivedCount_ = 0;
    afHeaderCode_ = second;
    if (afExpectedCount_ && afFrequency(second)) {
      afPairsExpected_ = static_cast<uint8_t>(afExpectedCount_ - 1U);
      afPairsReceived_ = 0;
      afState_ = afPairsExpected_ ? 1 : 0;
    } else {
      afState_ = 2;
      processAlternativeFrequencyByte(second);
    }
    publishAf(out);
    return;
  }

  processAlternativeFrequencyByte(first);
  processAlternativeFrequencyByte(second);
  publishAf(out);
}

void RdsDecoder::addEonAf(uint8_t networkIndex, uint8_t code) {
  if (networkIndex >= RDS_MAX_EON_NETWORKS) return;
  RdsEonNetwork& network = eonNetworks_[networkIndex];

  if (code == 224) {
    network.afCount = 0;
    network.afExpectedCount = 0;
    network.afLfMfPending = false;
    return;
  }
  if (code >= 225 && code <= 249) {
    network.afCount = 0;
    network.afExpectedCount = static_cast<uint8_t>(code - 224U);
    network.afLfMfPending = false;
    return;
  }
  if (code == 250) {
    network.afLfMfPending = true;
    return;
  }

  const uint32_t frequency = network.afLfMfPending
      ? lfmfFrequency(code) : afFrequency(code);
  network.afLfMfPending = false;
  if (!frequency || network.afCount >= 25) return;
  for (uint8_t i = 0; i < network.afCount; ++i)
    if (network.afKHz[i] == frequency) return;
  network.afKHz[network.afCount++] = frequency;
  if (network.afExpectedCount && network.afCount >= network.afExpectedCount)
    network.afExpectedCount = 0;
}

void RdsDecoder::processEon(uint8_t version, uint16_t blockB, uint16_t blockC,
                            uint16_t blockD, uint16_t pi, uint32_t tunedFrequencyKHz,
                            uint8_t blockCError, uint8_t blockDError,
                            RdsDecodedFields& out) {
  if (blockCError >= 3 || blockDError >= 3) return;

  // 14A: block C is PI(ON), block D is the selected information block.
  // 14B: block C is PI(TN), block D is PI(ON).
  if (version == 1 && pi && blockC != pi) return;
  const uint16_t otherPi = version == 0 ? blockC : blockD;
  if (!otherPi) return;

  uint8_t networkIndex = 0;
  RdsEonNetwork* net = findEon(eonNetworks_, otherPi, networkIndex);
  if (!net) return;
  if (!net->valid) {
    *net = RdsEonNetwork{};
    net->valid = true;
    net->pi = otherPi;
  }

  net->tp = (blockB & 0x0010U) != 0;
  if (version == 1) {
    net->ta = (blockB & 0x0008U) != 0;
  } else {
    const uint8_t variant = static_cast<uint8_t>(blockB & 0x000FU);
    const uint8_t high = static_cast<uint8_t>(blockD >> 8);
    const uint8_t low = static_cast<uint8_t>(blockD);
    const bool tunedBandI = tunedFrequencyKHz < 87500UL;

    switch (variant) {
      case 0: case 1: case 2: case 3: {
        const uint8_t offset = static_cast<uint8_t>(variant * 2U);
        setRdsChar(net->ps[offset], high);
        setRdsChar(net->ps[offset + 1U], low);
        net->ps[8] = '\0';
        break;
      }
      case 4:
        addEonAf(networkIndex, high);
        addEonAf(networkIndex, low);
        break;
      case 5: case 6: case 7: case 8: {
        const uint32_t tuning = afFrequency(high, tunedBandI);
        const uint32_t mapped = afFrequency(low, tunedBandI);
        if (tuning && mapped) {
          net->mappedTuningKHz = tuning;
          const uint8_t slot = static_cast<uint8_t>(variant - 5U);
          net->mappedFmKHz[slot] = mapped;
          net->mappedFmValid[slot] = true;
        }
        break;
      }
      case 9: {
        const uint32_t tuning = afFrequency(high, tunedBandI);
        const uint32_t mapped = lfmfFrequency(low);
        if (tuning && mapped) {
          net->mappedTuningKHz = tuning;
          net->mappedAmKHz = mapped;
          net->mappedAmValid = true;
        }
        break;
      }
      case 10: {
        // Variant 10 maps across Band I / Band II. The tuned frequency tells
        // the receiver which of the two 8-bit codes belongs to which band.
        const uint32_t tuning = afFrequency(high, tunedBandI);
        const uint32_t mapped = afFrequency(low, !tunedBandI);
        if (tuning && mapped) {
          net->mappedTuningKHz = tuning;
          net->mappedOtherBandKHz = mapped;
          net->mappedOtherBandValid = true;
        }
        break;
      }
      case 12:
        net->linkageValid = true;
        net->linkageActuator = (blockD & 0x8000U) != 0;
        net->extendedGeneric = (blockD & 0x4000U) != 0;
        net->internationalLink = (blockD & 0x1000U) != 0;
        net->linkageSetNumber = static_cast<uint16_t>(blockD & 0x0FFFU);
        break;
      case 13:
        net->pty = static_cast<uint8_t>((blockD >> 11) & 0x1FU);
        net->ta = (blockD & 0x0001U) != 0;
        break;
      // Variant 14 is reserved for future use in IEC 62106-2:2021.
      default:
        break;
    }
  }

  out.eon = *net;
  out.eonUpdated = true;
  out.eonCount = 0;
  for (uint8_t i = 0; i < RDS_MAX_EON_NETWORKS; ++i) {
    if (eonNetworks_[i].valid)
      out.eonNetworks[out.eonCount++] = eonNetworks_[i];
  }
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

  // PTYN has two four-character segments; only bit 0 is the segment address.
  const uint8_t segment = static_cast<uint8_t>(blockB & 0x0001U);
  const uint8_t offset = static_cast<uint8_t>(segment * 4U);
  const uint8_t bytes[4] = {
    static_cast<uint8_t>(blockC >> 8), static_cast<uint8_t>(blockC),
    static_cast<uint8_t>(blockD >> 8), static_cast<uint8_t>(blockD)
  };
  for (uint8_t i = 0; i < 4; ++i) setRdsChar(ptyn_[offset + i], bytes[i]);
  ptyn_[8] = '\0';
  ptynSegments_ |= static_cast<uint8_t>(1U << segment);
  if (ptynSegments_ == 0x03U) {
    memcpy(out.ptyn, ptyn_, sizeof(out.ptyn));
    out.ptynUpdated = true;
  }
}

void RdsDecoder::processOda(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                            uint8_t blockCError, uint8_t blockDError,
                            RdsDecodedFields& out) {
  if (blockCError >= 3 || blockDError >= 3) return;

  // In group 3A the five-bit Application Group Type Code is in block B
  // (the five bits following TP and PTY). Block C is application message data;
  // block D is the AID. For RT+ bit 14 of the message selects RT versus eRT.
  const uint8_t applicationGroupCode = static_cast<uint8_t>(blockB & 0x001FU);
  const uint8_t groupType = static_cast<uint8_t>((applicationGroupCode >> 1) & 0x0FU);
  const uint8_t groupVersion = static_cast<uint8_t>(applicationGroupCode & 0x01U);
  const uint16_t aid = blockD;
  out.odaUpdated = true;
  out.odaAid = aid;
  out.odaGroupType = groupType;
  out.odaGroupVersion = groupVersion;

  OdaAssignment* assignment = nullptr;
  OdaAssignment* empty = nullptr;
  for (uint8_t i = 0; i < 8; ++i) {
    if (odaAssignments_[i].valid && odaAssignments_[i].aid == aid) {
      assignment = &odaAssignments_[i];
      break;
    }
    if (!odaAssignments_[i].valid && !empty) empty = &odaAssignments_[i];
  }

  if (applicationGroupCode == 0x1FU) {
    // 11111 is an encoder data fault, not an instruction to withdraw an ODA.
    out.odaGroupType = 0xFF;
    out.odaGroupVersion = 0xFF;
  } else if (applicationGroupCode == 0) {
    if (assignment) *assignment = OdaAssignment{};
  } else {
    if (!assignment) assignment = empty;
    if (assignment) {
      assignment->valid = true;
      assignment->aid = aid;
      assignment->groupType = groupType;
      assignment->groupVersion = groupVersion;
      assignment->rtForEnhancedText = aid == 0x4BD7U && (blockC & 0x4000U) != 0;
    }
  }

  // Rebuild the RT+ dispatch independently of unrelated ODA assignments.
  rtPlusAid_ = 0;
  odaGroupType_ = odaGroupVersion_ = 0xFF;
  rtPlusForEnhancedText_ = false;
  for (uint8_t i = 0; i < 8; ++i) {
    if (odaAssignments_[i].valid && odaAssignments_[i].aid == 0x4BD7U) {
      rtPlusAid_ = 0x4BD7U;
      odaGroupType_ = odaAssignments_[i].groupType;
      odaGroupVersion_ = odaAssignments_[i].groupVersion;
      rtPlusForEnhancedText_ = odaAssignments_[i].rtForEnhancedText;
      break;
    }
  }
}

void RdsDecoder::processRtPlus(uint16_t blockB, uint16_t blockC, uint16_t blockD,
                               const char* radioText, uint8_t blockCError,
                               uint8_t blockDError, RdsDecodedFields& out) {
  if (!radioText || rtPlusAid_ != 0x4BD7U ||
      blockCError >= 3 || blockDError >= 3) return;

  // RT+ uses the 37 contiguous application bits: B[4:0], C[15:0], D[15:0].
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

  uint8_t textLength = 0;
  while (textLength < 64 && radioText[textLength] && radioText[textLength] != '\r')
    ++textLength;

  for (uint8_t i = 0; i < 2; ++i) {
    RdsRtPlusTag& tag = out.rtPlus[i];
    tag.contentType = types[i];
    tag.start = starts[i];
    tag.length = lengths[i];
    const uint16_t count = static_cast<uint16_t>(lengths[i]) + 1U;
    if (types[i] == 0 || starts[i] >= textLength ||
        count > static_cast<uint16_t>(textLength - starts[i])) continue;

    for (uint16_t n = 0; n < count && n < sizeof(tag.text) - 1; ++n) {
      const unsigned char ch = static_cast<unsigned char>(radioText[starts[i] + n]);
      if (ch == '\r' || ch == '\0') break;
      tag.text[n] = (ch >= 0x20U && ch <= 0x7EU) ? static_cast<char>(ch) : ' ';
    }
    tag.text[count] = '\0';
    tag.valid = true;
  }

  // IEC 62106-6 requires that the two tagged RT ranges do not overlap.
  if (out.rtPlus[0].valid && out.rtPlus[1].valid) {
    const uint16_t a0 = out.rtPlus[0].start;
    const uint16_t a1 = a0 + out.rtPlus[0].length + 1U;
    const uint16_t b0 = out.rtPlus[1].start;
    const uint16_t b1 = b0 + out.rtPlus[1].length + 1U;
    if (a0 < b1 && b0 < a1) {
      out.rtPlus[0].valid = false;
      out.rtPlus[1].valid = false;
      out.rtPlus[0].text[0] = out.rtPlus[1].text[0] = '\0';
    }
  }
  out.rtPlusUpdated = true;
}

void RdsDecoder::process(uint8_t groupType, uint8_t version, uint16_t blockB,
                         uint16_t blockC, uint16_t blockD, uint16_t pi,
                         uint32_t tunedFrequencyKHz,
                         uint8_t blockCError, uint8_t blockDError,
                         const char* radioText, RdsDecodedFields& out) {
  out.tpValid = true;
  out.ptyValid = true;
  out.tp = (blockB & 0x0400U) != 0;
  out.pty = static_cast<uint8_t>((blockB >> 5) & 0x1FU);

  if (groupType == 0 || (groupType == 15 && version == 1)) {
    out.taValid = out.msValid = out.diValid = true;
    out.ta = (blockB & 0x0010U) != 0;
    out.musicSpeech = (blockB & 0x0008U) != 0;
    out.decoderIdentification = (blockB & 0x0004U) != 0;
  } else if (groupType == 15 && version == 0) {
    out.taValid = true;
    out.ta = (blockB & 0x0010U) != 0;
  }

  if (groupType == 1 && version == 0 &&
      ((blockB >> 2) & 0x07U) == 0 && blockCError < 3) {
    const uint8_t ecc = static_cast<uint8_t>(blockC >> 8);
    if (ecc) { out.eccValid = true; out.ecc = ecc; }
  }
  if (groupType == 0 && version == 0 && blockCError < 3)
    processAlternativeFrequencies(blockC, out);
  if (groupType == 10 && version == 0)
    processPtyn(blockB, blockC, blockD, blockCError, blockDError, out);
  if (groupType == 14)
    processEon(version, blockB, blockC, blockD, pi, tunedFrequencyKHz,
               blockCError, blockDError, out);
  if (groupType == 3 && version == 0)
    processOda(blockB, blockC, blockD, blockCError, blockDError, out);

  // RT+ tagging for normal RadioText is carried only in an ODA type-A group.
  // eRT-tagged RT+ is a separate application and must not be read as normal RT.
  if (rtPlusAid_ == 0x4BD7U && !rtPlusForEnhancedText_ &&
      groupType == odaGroupType_ && version == odaGroupVersion_ && version == 0)
    processRtPlus(blockB, blockC, blockD, radioText,
                  blockCError, blockDError, out);
}
