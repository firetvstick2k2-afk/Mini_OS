#include "EiBiService.h"

#include <LittleFS.h>
#include <WiFi.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "../storage/StorageDiscovery.h"

namespace {
constexpr uint16_t kMinimumScheduleFrequency = 100;
constexpr uint16_t kMaximumScheduleFrequency = 60000;
}

void EiBiService::begin() {
  available_ = StorageDiscovery::ready() && LittleFS.exists(kSchedulePath);
  if (available_) strncpy(statusText_, "EiBi schedule ready", sizeof(statusText_) - 1);
}

bool EiBiService::beginDownload(bool wifiConnected) {
  if (downloading_) return false;
  if (!wifiConnected || WiFi.status() != WL_CONNECTED) {
    strncpy(statusText_, "EiBi: WLAN unavailable", sizeof(statusText_) - 1);
    return false;
  }
  if (!StorageDiscovery::ready()) {
    strncpy(statusText_, "EiBi: storage unavailable", sizeof(statusText_) - 1);
    return false;
  }

  http_.setTimeout(12000);
  http_.setConnectTimeout(12000);
  if (!http_.begin(kUrl)) {
    strncpy(statusText_, "EiBi: HTTP init failed", sizeof(statusText_) - 1);
    return false;
  }
  const int code = http_.GET();
  if (code != HTTP_CODE_OK) {
    http_.end();
    snprintf(statusText_, sizeof(statusText_), "EiBi: HTTP error %d", code);
    return false;
  }

  output_ = LittleFS.open(kTempPath, "w");
  if (!output_) {
    http_.end();
    strncpy(statusText_, "EiBi: cannot create temp file", sizeof(statusText_) - 1);
    return false;
  }

  bytesRead_ = 0;
  lineLength_ = 0;
  entryCount_ = 0;
  progressPercent_ = 0;
  contentLength_ = http_.getSize();
  streamFinished_ = false;
  downloading_ = true;
  strncpy(statusText_, "Downloading EiBi Schedules ...", sizeof(statusText_) - 1);
  statusText_[sizeof(statusText_) - 1] = '\0';
  Serial.printf("[EIBI] download started: %s
", kUrl);
  return true;
}

void EiBiService::update(bool wifiConnected, bool clockSynchronized, const struct tm& utc, uint32_t frequencyKHz) {
  if (downloading_) {
    if (!wifiConnected) {
      finishDownload(false, "EiBi: WLAN lost");
    } else {
      WiFiClient* stream = http_.getStreamPtr();
      uint16_t budget = 512;
      while (stream && budget-- && stream->available()) {
        const int value = stream->read();
        if (value < 0) break;
        ++bytesRead_;
        const char c = static_cast<char>(value);
        if (c == '
') {
          line_[lineLength_] = '\0';
          processLine();
          lineLength_ = 0;
        } else if (lineLength_ < sizeof(line_) - 1) {
          line_[lineLength_++] = c;
        } else {
          // Reject an overlong/corrupt row rather than parse a truncated one.
          lineLength_ = 0;
        }
      }

      if (contentLength_ > 0) {
        const uint8_t percent = static_cast<uint8_t>(
          min<uint32_t>(99, (bytesRead_ * 100UL) / static_cast<uint32_t>(contentLength_)));
        if (percent != progressPercent_) {
          progressPercent_ = percent;
          snprintf(statusText_, sizeof(statusText_), "Downloading EiBi Schedules %u%%",
                   static_cast<unsigned>(progressPercent_));
        }
      }

      if (stream && !http_.connected() && !stream->available()) streamFinished_ = true;
      if (contentLength_ >= 0 && bytesRead_ >= static_cast<size_t>(contentLength_))
        streamFinished_ = true;

      if (streamFinished_) {
        if (lineLength_) {
          line_[lineLength_] = '\0';
          processLine();
          lineLength_ = 0;
        }
        finishDownload(entryCount_ > 0, entryCount_ > 0 ? "EiBi schedule updated" : "EiBi: no valid entries");
      }
    }
  }

  if (!downloading_ && available_ && clockSynchronized && frequencyKHz > 0 &&
      frequencyKHz <= UINT16_MAX) {
    const uint32_t minuteKey = static_cast<uint32_t>(utc.tm_yday) * 1440UL +
                               static_cast<uint32_t>(utc.tm_hour) * 60UL +
                               static_cast<uint32_t>(utc.tm_min);
    if (minuteKey != lastLookupMinute_ || frequencyKHz != lastLookupFrequency_) {
      lastLookupMinute_ = minuteKey;
      lastLookupFrequency_ = frequencyKHz;
      lookup(frequencyKHz, static_cast<uint8_t>(utc.tm_hour),
             static_cast<uint8_t>(utc.tm_min));
    }
  }
}

void EiBiService::processLine() {
  char* p = line_;
  while (*p && static_cast<unsigned char>(*p) <= ' ') ++p;
  if (!isdigit(static_cast<unsigned char>(*p))) return;
  ScheduleEntry entry{};
  if (!parseLine(p, entry)) return;
  if (output_.write(reinterpret_cast<const uint8_t*>(&entry), sizeof(entry)) == sizeof(entry))
    ++entryCount_;
}

bool EiBiService::parseLine(const char* line, ScheduleEntry& entry) const {
  char frequencyText[15] = {};
  char timeText[10] = {};
  char ignoredDays[12] = {};
  char nameText[25] = {};
  if (sscanf(line, "%14c%9c%11c%24c", frequencyText, timeText, ignoredDays, nameText) != 4)
    return false;
  frequencyText[14] = '\0';
  timeText[9] = '\0';
  nameText[24] = '\0';

  const long frequency = static_cast<long>(atof(frequencyText));
  if (frequency < kMinimumScheduleFrequency || frequency > kMaximumScheduleFrequency)
    return false;

  int sh = -1, sm = 0, eh = -1, em = 0;
  if (sscanf(timeText, "%2d%2d-%2d%2d", &sh, &sm, &eh, &em) != 4 ||
      sh < 0 || sh > 23 || eh < 0 || eh > 23 || sm < 0 || sm > 59 || em < 0 || em > 59)
    return false;

  if (strstr(nameText, "Jammer")) return false;
  char* first = nameText;
  while (*first == ' ' || *first == '\t') ++first;
  char* end = first + strlen(first);
  while (end > first && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) --end;
  *end = '\0';
  if (!*first) return false;

  entry.frequencyKHz = static_cast<uint16_t>(frequency);
  entry.startHour = static_cast<int8_t>(sh);
  entry.startMinute = static_cast<int8_t>(sm);
  entry.endHour = static_cast<int8_t>(eh);
  entry.endMinute = static_cast<int8_t>(em);
  size_t out = 0;
  for (const unsigned char* q = reinterpret_cast<const unsigned char*>(first);
       *q && out < sizeof(entry.name) - 1; ++q) {
    entry.name[out++] = replaceAccent(static_cast<char>(*q));
  }
  entry.name[out] = '\0';
  return out > 0;
}

char EiBiService::replaceAccent(char c) {
  switch (static_cast<uint8_t>(c)) {
    case 0xE1: case 0xE0: case 0xE2: case 0xE3: case 0xE4: return 'a';
    case 0xE9: case 0xE8: case 0xEA: case 0xEB: return 'e';
    case 0xED: case 0xEC: case 0xEE: case 0xEF: return 'i';
    case 0xF3: case 0xF2: case 0xF4: case 0xF5: case 0xF6: return 'o';
    case 0xFA: case 0xF9: case 0xFB: case 0xFC: return 'u';
    case 0xC1: case 0xC0: case 0xC2: case 0xC3: case 0xC4: return 'A';
    case 0xC9: case 0xC8: case 0xCA: case 0xCB: return 'E';
    case 0xCD: case 0xCC: case 0xCE: case 0xCF: return 'I';
    case 0xD3: case 0xD2: case 0xD4: case 0xD5: case 0xD6: return 'O';
    case 0xDA: case 0xD9: case 0xDB: case 0xDC: return 'U';
    case 0xF1: return 'n'; case 0xD1: return 'N';
    case 0xE7: return 'c'; case 0xC7: return 'C';
    default: return c;
  }
}

bool EiBiService::entryIsNow(const ScheduleEntry& entry, int now) {
  if (entry.startHour < 0 || entry.endHour < 0) return true;
  const int start = entry.startHour * 60 + entry.startMinute;
  const int end = entry.endHour * 60 + entry.endMinute;
  if (start <= end) return now >= start && now <= end;
  return now >= start || now <= end;
}

void EiBiService::lookup(uint32_t frequencyKHz, uint8_t hour, uint8_t minute) {
  stationName_[0] = '\0';
  if (!available_ || frequencyKHz > UINT16_MAX) return;
  File file = LittleFS.open(kSchedulePath, "r");
  if (!file) return;
  const size_t count = file.size() / sizeof(ScheduleEntry);
  if (!count) { file.close(); return; }

  size_t low = 0, high = count;
  while (low < high) {
    const size_t mid = low + (high - low) / 2;
    ScheduleEntry entry{};
    if (!file.seek(mid * sizeof(entry), SeekSet) ||
        file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(entry)) != sizeof(entry)) {
      file.close();
      return;
    }
    if (entry.frequencyKHz < frequencyKHz) low = mid + 1;
    else high = mid;
  }

  const int now = hour * 60 + minute;
  for (size_t i = low; i < count; ++i) {
    ScheduleEntry entry{};
    if (!file.seek(i * sizeof(entry), SeekSet) ||
        file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(entry)) != sizeof(entry))
      break;
    if (entry.frequencyKHz != frequencyKHz) break;
    if (entryIsNow(entry, now)) {
      strncpy(stationName_, entry.name, sizeof(stationName_) - 1);
      stationName_[sizeof(stationName_) - 1] = '\0';
      break;
    }
  }
  file.close();
}

void EiBiService::finishDownload(bool success, const char* message) {
  if (output_) {
    output_.flush();
    output_.close();
  }
  http_.end();
  downloading_ = false;
  if (success && entryCount_ > 0) {
    LittleFS.remove(kBackupPath);
    const bool hadOld = LittleFS.exists(kSchedulePath);
    if (hadOld && !LittleFS.rename(kSchedulePath, kBackupPath)) {
      LittleFS.remove(kTempPath);
      strncpy(statusText_, "EiBi: cannot stage old file", sizeof(statusText_) - 1);
      available_ = LittleFS.exists(kSchedulePath);
      return;
    }
    if (!LittleFS.rename(kTempPath, kSchedulePath)) {
      if (hadOld) LittleFS.rename(kBackupPath, kSchedulePath);
      LittleFS.remove(kTempPath);
      strncpy(statusText_, "EiBi: install failed", sizeof(statusText_) - 1);
      available_ = LittleFS.exists(kSchedulePath);
      return;
    }
    LittleFS.remove(kBackupPath);
    available_ = true;
    progressPercent_ = 100;
    lastLookupFrequency_ = UINT32_MAX;
    strncpy(statusText_, message, sizeof(statusText_) - 1);
    Serial.printf("[EIBI] updated: %lu entries, %lu bytes
",
                  static_cast<unsigned long>(entryCount_),
                  static_cast<unsigned long>(bytesRead_));
  } else {
    LittleFS.remove(kTempPath);
    available_ = LittleFS.exists(kSchedulePath);
    strncpy(statusText_, message, sizeof(statusText_) - 1);
    Serial.printf("[EIBI] failed: %s (%lu bytes, %lu entries)
", message,
                  static_cast<unsigned long>(bytesRead_),
                  static_cast<unsigned long>(entryCount_));
  }
  statusText_[sizeof(statusText_) - 1] = '\0';
}

