#include "TcpRemoteService.h"

#include <stdlib.h>
#include <string.h>
#include <lwip/sockets.h>

namespace {
constexpr uint8_t kSsbBandwidthCount = 6;

bool parseUnsigned(const char* text, uint32_t& value) {
  if (!text || !text[0]) return false;
  char* end = nullptr;
  const unsigned long parsed = strtoul(text, &end, 10);
  if (!end || *end || parsed > UINT32_MAX) return false;
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseMode(const char* text, RadioModulation& mode) {
  if (!text) return false;
  if (!strcmp(text, "FM")) mode = RadioModulation::FM;
  else if (!strcmp(text, "AM")) mode = RadioModulation::AM;
  else if (!strcmp(text, "LSB")) mode = RadioModulation::LSB;
  else if (!strcmp(text, "USB")) mode = RadioModulation::USB;
  else return false;
  return true;
}
}

void TcpRemoteService::begin(RadioCore* radio, MemoryService* memories,
                             DisplayDriver* display, SettingsStore* settings,
                             EncoderInput* encoder) {
  stop();
  radio_ = radio;
  memories_ = memories;
  display_ = display;
  settings_ = settings;
  encoder_ = encoder;
}

void TcpRemoteService::stop() {
  if (persistPending_) persistRadioState();
  persistPending_ = false;
  persistDueMs_ = 0;
  client_.stop();
  if (serverStarted_) server_.end();
  serverStarted_ = false;
  remoteLogOn_ = false;
  lineCommand_ = 0;
  lineLength_ = 0;
  discardLine_ = false;
}

void TcpRemoteService::update(bool networkAvailable, bool enabled) {
  if (!enabled || !networkAvailable || !radio_ || !radio_->isReady()) {
    stop();
    return;
  }

  const uint32_t now = millis();
  if (!serverStarted_) {
    if (now - lastListenAttemptMs_ < 1000) return;
    lastListenAttemptMs_ = now;
    server_.begin();
    if (!server_) {
      server_.end();
      return;
    }
    server_.setNoDelay(false);
    serverStarted_ = true;
    Serial.printf("[TCP] remote control listening on port %u\n", PORT);
  }

  if (!client_.connected()) client_.stop();
  if (!client_) {
    WiFiClient incoming = server_.accept();
    if (incoming) {
      client_ = incoming;
      lineCommand_ = 0;
      lineLength_ = 0;
      discardLine_ = false;
      remoteLogOn_ = false;
      remoteSequence_ = 0;
      Serial.println("[TCP] remote controller connected");
    }
  }

  if (!client_.connected()) return;
  while (client_.available()) {
    const int raw = client_.read();
    if (raw < 0) break;
    const char value = static_cast<char>(raw);
    if (lineCommand_) {
      if (value == '\r' || value == '\n') {
        if (!discardLine_) {
          line_[lineLength_] = '\0';
          processLineCommand(lineCommand_, line_);
        } else {
          sendError("Command line too long");
        }
        lineCommand_ = 0;
        lineLength_ = 0;
        discardLine_ = false;
      } else if (!discardLine_) {
        if (lineLength_ + 1 < sizeof(line_)) line_[lineLength_++] = value;
        else discardLine_ = true;
      }
      continue;
    }

    if (value == '\n') continue;
    if (value == 'F' || value == '#') {
      lineCommand_ = value;
      lineLength_ = 0;
      discardLine_ = false;
    } else {
      processCommand(value);
    }
  }

  if (client_.connected() && !client_.available() && client_.fd() >= 0) {
    uint8_t byte = 0;
    if (recv(client_.fd(), &byte, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
      client_.stop();
      Serial.println("[TCP] remote controller disconnected");
    }
  }

  if (client_.connected() && remoteLogOn_ && now - lastStatusMs_ >= 500) {
    lastStatusMs_ = now;
    ++remoteSequence_;
    printStatus();
  }
  if (persistPending_ && now - persistDueMs_ >= 2000) {
    persistRadioState();
    persistPending_ = false;
    persistDueMs_ = 0;
  }
}

const char* TcpRemoteService::modeName(RadioModulation mode) {
  switch (mode) {
    case RadioModulation::FM: return "FM";
    case RadioModulation::AM: return "AM";
    case RadioModulation::LSB: return "LSB";
    case RadioModulation::USB: return "USB";
    default: return "?";
  }
}

void TcpRemoteService::printStatus() {
  if (!client_ || !radio_) return;
  const RadioSnapshot& state = radio_->snapshot();
  const uint32_t remoteFrequency = state.modulation == RadioModulation::FM
    ? state.frequencyKHz / 10U : state.frequencyKHz;
  client_.printf("%u,%lu,%d,%d,%s,%s,%u,%u,%u,%u,%d,%d,%u,%.2f,%u\r\n",
    0U,
    static_cast<unsigned long>(remoteFrequency),
    static_cast<int>(state.bfoOffsetHz),
    0,
    state.bandName,
    modeName(state.modulation),
    static_cast<unsigned>(state.tuningStepKHz),
    static_cast<unsigned>(state.modulation == RadioModulation::LSB ||
                          state.modulation == RadioModulation::USB
                            ? radio_->ssbBandwidthChoice() : radio_->bandwidthChoice()),
    static_cast<unsigned>(radio_->agcGainIndex()),
    static_cast<unsigned>(radio_->volume()),
    static_cast<int>(state.rssi),
    static_cast<int>(state.snr),
    0U,
    0.0,
    static_cast<unsigned>(remoteSequence_));
}

void TcpRemoteService::printMemories() {
  if (!client_ || !memories_) return;
  for (uint8_t i = 0; i < MemoryService::kSlotCount; ++i) {
    const MemoryService::Entry& entry = memories_->entry(i);
    if (!entry.frequencyKHz || entry.bandIndex >= BandManager::count()) continue;
    const RadioBandDefinition& band = BandManager::at(entry.bandIndex);
    const RadioModulation mode = static_cast<RadioModulation>(entry.modulation);
    const uint32_t remoteFrequency = mode == RadioModulation::FM
      ? entry.frequencyKHz / 10U : entry.frequencyKHz;
    client_.printf("#%02u,%s,%lu,%s", static_cast<unsigned>(i + 1),
      band.name, static_cast<unsigned long>(remoteFrequency), modeName(mode));
    if (entry.name[0]) client_.printf(",%s", entry.name);
    client_.print("\r\n");
  }
}

void TcpRemoteService::sendError(const char* message) {
  if (client_) client_.printf("\r\nError: %s\r\n", message ? message : "Command failed");
}

void TcpRemoteService::persistRadioState() {
  if (!radio_ || !settings_) return;
  const RadioSnapshot& state = radio_->snapshot();
  if (state.modulation == RadioModulation::FM)
    settings_->saveLastFmFrequencyKHz(state.frequencyKHz);
  settings_->saveVolume(radio_->volume());
  settings_->saveAgcAutomatic(radio_->agcAutomatic());
  settings_->saveAgcGainIndex(radio_->agcGainIndex());
  settings_->saveAttenuationIndex(radio_->attenuationIndex());
  settings_->saveBfoOffsetHz(radio_->bfoOffsetHz());
  settings_->saveSsbBandwidthChoice(radio_->ssbBandwidthChoice());
}

bool TcpRemoteService::cycleModulation(int8_t direction) {
  if (!radio_) return false;
  const RadioModulation modes[] = {
    RadioModulation::FM, RadioModulation::AM,
    RadioModulation::LSB, RadioModulation::USB
  };
  const int current = static_cast<int>(radio_->snapshot().modulation);
  for (int step = 1; step <= 4; ++step) {
    int candidate = (current + direction * step) % 4;
    if (candidate < 0) candidate += 4;
    const RadioModulation mode = modes[candidate];
    if (radio_->canUseModulation(mode) && radio_->setModulation(mode)) return true;
  }
  return false;
}

bool TcpRemoteService::setFrequency(const char* value) {
  uint32_t frequencyHz = 0;
  if (!parseUnsigned(value, frequencyHz) || frequencyHz < 100000UL ||
      frequencyHz > 300000000UL) {
    sendError("Invalid frequency");
    return false;
  }
  if (!radio_->tune(frequencyHz / 1000UL)) {
    sendError("Frequency is outside the current band's limits");
    return false;
  }
  const RadioModulation mode = radio_->snapshot().modulation;
  const int16_t bfo = (mode == RadioModulation::LSB || mode == RadioModulation::USB)
    ? static_cast<int16_t>(frequencyHz % 1000UL) : 0;
  if (!radio_->setBfoOffsetHz(bfo)) {
    sendError("BFO setting failed");
    return false;
  }
  client_.print('F');
  client_.print(value);
  client_.print("\r\n");
  persistPending_ = true;
  persistDueMs_ = millis();
  if (display_) display_->notifyTuning();
  return true;
}

bool TcpRemoteService::setMemory(char* value) {
  if (!memories_ || !value) {
    sendError("Memory service unavailable");
    return false;
  }
  char* slotText = value;
  char* bandText = strchr(slotText, ',');
  if (!bandText) { sendError("Expected slot,band,frequency,mode"); return false; }
  *bandText++ = '\0';
  char* frequencyText = strchr(bandText, ',');
  if (!frequencyText) { sendError("Expected band and frequency"); return false; }
  *frequencyText++ = '\0';
  char* modeText = strchr(frequencyText, ',');
  if (!modeText) { sendError("Expected modulation"); return false; }
  *modeText++ = '\0';
  char* nameText = strchr(modeText, ',');
  if (nameText) *nameText++ = '\0';

  uint32_t slotNumber = 0, frequencyKHz = 0;
  if (!parseUnsigned(slotText, slotNumber) || slotNumber < 1 ||
      slotNumber > MemoryService::kSlotCount ||
      !parseUnsigned(frequencyText, frequencyKHz)) {
    sendError("Invalid memory slot or frequency");
    return false;
  }
  const uint8_t slot = static_cast<uint8_t>(slotNumber - 1);
  RadioModulation mode;
  if (!parseMode(modeText, mode)) { sendError("Unknown modulation"); return false; }
  if (frequencyKHz == 0) {
    if (!memories_->erase(slot)) { sendError("Could not clear memory slot"); return false; }
    client_.printf("#%lu,%s,0,%s\r\n", static_cast<unsigned long>(slotNumber), bandText, modeText);
    return true;
  }
  // ATS-Mini-Dev's memory protocol uses 10-kHz units for FM, but kHz
  // for AM/SSB. Mini_OS stores every modulation internally in kHz.
  if (mode == RadioModulation::FM) {
    if (frequencyKHz > UINT32_MAX / 10U) {
      sendError("Invalid FM memory frequency");
      return false;
    }
    frequencyKHz *= 10U;
  }
  int bandIndex = -1;
  for (uint8_t i = 0; i < BandManager::count(); ++i) {
    const RadioBandDefinition& band = BandManager::at(i);
    if (!strcmp(band.name, bandText) && BandManager::contains(i, frequencyKHz)) {
      bandIndex = i;
      break;
    }
  }
  if (bandIndex < 0) { sendError("Frequency does not match the named band"); return false; }

  RadioSnapshot stored{};
  stored.frequencyKHz = frequencyKHz;
  stored.modulation = mode;
  strncpy(stored.bandName, BandManager::at(static_cast<uint8_t>(bandIndex)).name,
          sizeof(stored.bandName) - 1);
  if (nameText) strncpy(stored.stationName, nameText, sizeof(stored.stationName) - 1);
  if (!memories_->save(slot, stored, static_cast<uint8_t>(bandIndex),
                       nameText && nameText[0] ? nameText : nullptr)) {
    sendError("Could not save memory slot");
    return false;
  }
  client_.print('#');
  client_.print(value);
  client_.print("\r\n");
  return true;
}

void TcpRemoteService::processLineCommand(char command, char* line) {
  if (command == 'F') setFrequency(line);
  else if (command == '#') setMemory(line);
}

void TcpRemoteService::processCommand(char command) {
  if (!radio_ || !radio_->isReady()) return;
  bool changed = false;
  switch (command) {
    case 'R':
      if (encoder_) encoder_->inject(EncoderEventType::RotateClockwise, 1);
      return;
    case 'r':
      if (encoder_) encoder_->inject(EncoderEventType::RotateCounterClockwise, -1);
      return;
    case 'e':
    case 'E':
      if (encoder_) encoder_->inject(EncoderEventType::ShortPress, 0);
      return;
    case 'B': changed = radio_->nextBand(1); break;
    case 'b': changed = radio_->nextBand(-1); break;
    case 'M': changed = cycleModulation(1); break;
    case 'm': changed = cycleModulation(-1); break;
    case 'S': {
      const uint8_t count = radio_->stepChoiceCount();
      const uint8_t next = count ? static_cast<uint8_t>((radio_->stepChoice() + 1) % count) : 0;
      changed = count && radio_->setStepChoice(next);
      break;
    }
    case 's': {
      const uint8_t count = radio_->stepChoiceCount();
      const uint8_t next = count ? static_cast<uint8_t>((radio_->stepChoice() + count - 1) % count) : 0;
      changed = count && radio_->setStepChoice(next);
      break;
    }
    case 'W':
    case 'w': {
      const bool ssb = radio_->snapshot().modulation == RadioModulation::LSB ||
                       radio_->snapshot().modulation == RadioModulation::USB;
      if (ssb) {
        const uint8_t current = radio_->ssbBandwidthChoice();
        const uint8_t next = command == 'W'
          ? static_cast<uint8_t>((current + 1) % kSsbBandwidthCount)
          : static_cast<uint8_t>((current + kSsbBandwidthCount - 1) % kSsbBandwidthCount);
        changed = radio_->setSsbBandwidthChoice(next);
      } else {
        const uint8_t count = radio_->bandwidthChoiceCount();
        const uint8_t current = radio_->bandwidthChoice();
        const uint8_t next = command == 'W'
          ? static_cast<uint8_t>((current + 1) % (count ? count : 1))
          : static_cast<uint8_t>((current + (count ? count : 1) - 1) % (count ? count : 1));
        changed = count && radio_->setBandwidthChoice(next);
      }
      break;
    }
    case 'A':
    case 'a': {
      const int direction = command == 'A' ? 1 : -1;
      if (radio_->agcAutomatic()) {
        changed = radio_->setAgcAutomatic(false) &&
                  radio_->setAgcManualGain(radio_->agcGainIndex());
      } else {
        int next = static_cast<int>(radio_->agcGainIndex()) + direction;
        if (next < 0) next = 0;
        if (next > 24) next = 24;
        changed = radio_->setAgcManualGain(static_cast<uint8_t>(next));
      }
      break;
    }
    case 'V':
    case 'v': {
      int next = static_cast<int>(radio_->volume()) + (command == 'V' ? 1 : -1);
      if (next < 0) next = 0;
      if (next > 63) next = 63;
      changed = radio_->setVolume(static_cast<uint8_t>(next));
      break;
    }
    case 'L':
    case 'l': {
      if (!display_) break;
      int next = static_cast<int>(display_->brightness()) + (command == 'L' ? 10 : -10);
      if (next < 10) next = 10;
      if (next > 255) next = 255;
      display_->setBrightness(static_cast<uint8_t>(next));
      changed = true;
      break;
    }
    case 't':
      remoteLogOn_ = !remoteLogOn_;
      if (client_) client_.println(remoteLogOn_ ? "Remote status logging enabled" : "Remote status logging disabled");
      return;
    case '$':
      printMemories();
      return;
    case 'O':
    case 'o': sendError("Sleep control is not implemented in Mini_OS"); return;
    case 'I':
    case 'i': sendError("Calibration control is not implemented in Mini_OS"); return;
    case 'C':
    case 'c': sendError("Screenshot transfer is not implemented in Mini_OS"); return;
    case 'T':
    case '^':
    case '@': sendError("Custom theme editing is not implemented in Mini_OS"); return;
    default: return;
  }

  if (!changed) {
    sendError("Command is not available for the current band or mode");
    return;
  }
  persistPending_ = true;
  persistDueMs_ = millis();
  if (display_) display_->notifyTuning();
}
