#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include "../radio/RadioCore.h"
#include "../radio/MemoryService.h"
#include "../display/DisplayDriver.h"
#include "../settings/SettingsStore.h"
#include "../input/EncoderInput.h"

// ATS-Mini-Dev compatible TCP control endpoint (port 60000).
// The listener is opt-in and is only serviced while a WLAN interface is up.
class TcpRemoteService {
public:
  void begin(RadioCore* radio, MemoryService* memories, DisplayDriver* display,
             SettingsStore* settings, EncoderInput* encoder);
  void update(bool networkAvailable, bool enabled);
  void stop();

private:
  static constexpr uint16_t PORT = 60000;
  static constexpr size_t LINE_CAPACITY = 96;

  WiFiServer server_{PORT, 1};
  WiFiClient client_;
  RadioCore* radio_ = nullptr;
  MemoryService* memories_ = nullptr;
  DisplayDriver* display_ = nullptr;
  SettingsStore* settings_ = nullptr;
  EncoderInput* encoder_ = nullptr;
  char line_[LINE_CAPACITY] = {};
  size_t lineLength_ = 0;
  char lineCommand_ = 0;
  bool discardLine_ = false;
  bool serverStarted_ = false;
  bool remoteLogOn_ = false;
  uint8_t remoteSequence_ = 0;
  uint32_t lastListenAttemptMs_ = 0;
  uint32_t lastStatusMs_ = 0;
  uint32_t persistDueMs_ = 0;
  bool persistPending_ = false;

  void processCommand(char command);
  void processLineCommand(char command, char* line);
  void printStatus();
  void printMemories();
  void sendError(const char* message);
  bool setFrequency(const char* value);
  bool setMemory(char* value);
  bool cycleModulation(int8_t direction);
  void persistRadioState();
  static const char* modeName(RadioModulation mode);
};
