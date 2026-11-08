#pragma once
#include <Arduino.h>
#include <HTTPClient.h>
#include <FS.h>
#include <time.h>

class EiBiService {
public:
  void begin();
  bool beginDownload(bool wifiConnected);
  void update(bool wifiConnected, bool clockSynchronized, const struct tm& utc, uint32_t frequencyKHz);
  bool downloading() const { return downloading_; }
  bool available() const { return available_; }
  const char* statusText() const { return statusText_; }
  const char* stationName() const { return stationName_; }
  uint8_t progressPercent() const { return progressPercent_; }

private:
  struct ScheduleEntry {
    uint16_t frequencyKHz;
    int8_t startHour;
    int8_t startMinute;
    int8_t endHour;
    int8_t endMinute;
    char name[32];
  };

  HTTPClient http_;
  File output_;
  char line_[200] = {};
  size_t lineLength_ = 0;
  size_t bytesRead_ = 0;
  int contentLength_ = -1;
  uint32_t entryCount_ = 0;
  uint32_t lastLookupMinute_ = UINT32_MAX;
  uint32_t lastLookupFrequency_ = UINT32_MAX;
  uint8_t progressPercent_ = 0;
  bool downloading_ = false;
  bool available_ = false;
  bool streamFinished_ = false;
  char statusText_[64] = "EiBi not loaded";
  char stationName_[32] = {};

  void processLine();
  bool parseLine(const char* line, ScheduleEntry& entry) const;
  void finishDownload(bool success, const char* message);
  void lookup(uint32_t frequencyKHz, uint8_t hour, uint8_t minute);
  static bool entryIsNow(const ScheduleEntry& entry, int minuteOfDay);
  static char replaceAccent(char c);
};
