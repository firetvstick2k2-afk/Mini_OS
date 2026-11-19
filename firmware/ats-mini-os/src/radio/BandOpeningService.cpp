#include "BandOpeningService.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../system/BootDiagnostics.h"

namespace {
constexpr char kFeedUrl[] = "https://www.mapforham.com/hf-propagation/data/solar.json";
constexpr char kFmMufUrl[] = "https://fmdx.org/includes/tools/get_muf.php";
constexpr uint32_t kRefreshIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kRetryIntervalMs = 5UL * 60UL * 1000UL;
constexpr size_t kMaxResponseBytes = 16384;
// These are the exact receive windows in BandManager.cpp, represented in kHz.
// Rows without a matching Map For HAM band_detail entry intentionally remain grey.
const BandOpeningService::Band kBands[BandOpeningService::kBandCount] = {
  {"LW Band", "SWL", 100, 495, 0},
  {"MW1 Band", "SWL", 100, 1800, 0},
  {"MW2 Band", "SWL", 495, 1701, 0},
  {"MW3 Band", "SWL", 1700, 3500, 0},
  {"160m Band", "AFU", 1800, 2000, 160},
  {"90m Band", "SWL", 3200, 3400, 0},
  {"80m Band", "AFU", 3500, 4000, 80},
  {"75m Band", "SWL", 3900, 4000, 0},
  {"60m Band", "AFU", 4700, 5100, 60},
  {"49m Band", "SWL", 5900, 6200, 0},
  {"40m Band", "AFU", 7000, 7300, 40},
  {"41m Band", "SWL", 7200, 7500, 0},
  {"31m Band", "SWL", 9400, 9900, 0},
  {"30m Band", "AFU", 10000, 10200, 30},
  {"25m Band", "SWL", 11600, 12100, 0},
  {"22m Band", "SWL", 13500, 13900, 0},
  {"20m Band", "AFU", 14000, 14400, 20},
  {"19m Band", "SWL", 15100, 15900, 0},
  {"17m Band", "AFU", 18000, 18200, 17},
  {"16m Band", "SWL", 17400, 18100, 0},
  {"15m Band (BC)", "SWL", 18900, 19100, 0},
  {"15m Band (AFU)", "AFU", 21000, 21500, 15},
  {"13m Band", "SWL", 21500, 21900, 0},
  {"12m Band", "AFU", 24800, 25000, 12},
  {"11m Band", "CB", 25600, 26100, 0},
  {"10m Band", "AFU", 28000, 29700, 10},
  {"2m Band", "AFU", 144000, 146000, 0},
  {"70cm Band", "AFU", 430000, 440000, 0},
  {"UKW / FM", "FM-DX", 64000, 108000, 0}
};


int64_t civilDays(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(year - era * 400);
  const unsigned mp = month > 2 ? month - 3 : month + 9;
  const unsigned doy = (153 * mp + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<int64_t>(era) * 146097 + doe - 719468;
}
bool timestampIsFresh(const char* value) {
  int year=0, month=0, day=0, hour=0, minute=0, second=0;
  if (!value || sscanf(value,"%4d-%2d-%2dT%2d:%2d:%2d",&year,&month,&day,&hour,&minute,&second)!=6)
    return false;
  if (year < 2024 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return false;
  const int64_t days=civilDays(year,static_cast<unsigned>(month),static_cast<unsigned>(day));
  int64_t epoch=days*86400LL+hour*3600LL+minute*60LL+second;
  const size_t length=strlen(value);
  if (length>=6) {
    const char* zone=value+length-6;
    if ((*zone=='+' || *zone=='-') && zone[3]==':') {
      const int offsetHours=(zone[1]-'0')*10+(zone[2]-'0');
      const int offsetMinutes=(zone[4]-'0')*10+(zone[5]-'0');
      const int offset=(offsetHours*60+offsetMinutes)*60;
      epoch += (*zone=='+') ? -offset : offset;
    }
  }
  const time_t now=time(nullptr);
  // Without a synchronized system clock, the feed timestamp cannot be aged reliably.
  if (now < 1700000000) return true;
  const int64_t age=static_cast<int64_t>(now)-epoch;
  // The feed normally refreshes every ~5 minutes, but serves its last valid
  // upstream values during outages. Keep a 2-hour grace window so a temporary
  // source outage does not blank every band; older data is still rejected.
  return age >= -300 && age <= 2*60*60;
}

BandOpeningService::Status parseStatus(JsonVariantConst value) {
  if (!value.is<const char*>()) return BandOpeningService::Status::Unknown;
  const char* text = value.as<const char*>();
  if (!text) return BandOpeningService::Status::Unknown;
  if (strcmp(text, "Poor") == 0) return BandOpeningService::Status::Poor;
  if (strcmp(text, "Fair") == 0) return BandOpeningService::Status::Fair;
  if (strcmp(text, "Good") == 0) return BandOpeningService::Status::Good;
  return BandOpeningService::Status::Unknown;
}
}

const BandOpeningService::Band& BandOpeningService::band(uint8_t index) {
  return kBands[index < kBandCount ? index : 0];
}
const char* BandOpeningService::statusText(Status status) {
  switch (status) {
    case Status::Poor: return "SCHWACH";
    case Status::Fair: return "MITTEL";
    case Status::Good: return "GUT";
    default: return "KEINE DATEN";
  }
}
bool BandOpeningService::begin() {
  if (taskHandle_) return true;
  Snapshot initial;
  publish(initial);
  const BaseType_t created = xTaskCreatePinnedToCore(taskEntry, "BandOpenWorker", 12288, this, 1, &taskHandle_, 0);
  BootDiagnostics::printf("[BANDOPEN] worker %s\n", created == pdPASS ? "created" : "creation failed");
  return created == pdPASS;
}
void BandOpeningService::update(bool wifiConnected) {
  const bool wasConnected = wifiConnected_;
  wifiConnected_ = wifiConnected;
  Snapshot connectionState = snapshot();
  connectionState.waitingForWifi = !wifiConnected;
  if (wifiConnected) connectionState.fetchFailed = false;
  publish(connectionState);
  // Opening the monitor (or regaining router access) must trigger a fetch
  // immediately instead of waiting for the previous five-minute cadence.
  if (wifiConnected && !wasConnected) {
    refreshRequested_ = true;
    BootDiagnostics::write("[BANDOPEN] Wi-Fi available; immediate refresh requested");
  }
  if (wifiConnected != wasConnected) {
    BootDiagnostics::printf("[BANDOPEN][GATE] monitor_data_access=%s wifi_status=%d snapshot_valid=%u",
      wifiConnected ? "ON" : "OFF", static_cast<int>(WiFi.status()), snapshot().valid ? 1U : 0U);
  }
  if (!wifiConnected) {
    Snapshot current = snapshot();
    if (current.valid || current.fmMufValid) {
      current.stale = true;
      current.fmMufStale = true;
      publish(current);
    }
  }
}
BandOpeningService::Snapshot BandOpeningService::snapshot() const {
  Snapshot copy;
  portENTER_CRITICAL(&snapshotMux_); copy = snapshot_; portEXIT_CRITICAL(&snapshotMux_);
  return copy;
}
void BandOpeningService::publish(const Snapshot& value) {
  portENTER_CRITICAL(&snapshotMux_); snapshot_ = value; portEXIT_CRITICAL(&snapshotMux_);
}
void BandOpeningService::taskEntry(void* context) {
  static_cast<BandOpeningService*>(context)->taskLoop();
}
void BandOpeningService::taskLoop() {
  BootDiagnostics::write("[BANDOPEN] worker entered");
  for (;;) {
    const uint32_t now = millis();
    const bool due = refreshRequested_ || !lastAttemptMs_ || now - lastAttemptMs_ >= kRefreshIntervalMs;
    if (wifiConnected_ && (!lastMufAttemptMs_ || now - lastMufAttemptMs_ >= 60UL * 1000UL)) {
      lastMufAttemptMs_ = now;
      float muf = 0.0f;
      BootDiagnostics::printf("[BANDOPEN][FM_MUF] cycle start wifi=%d heap=%u",
        static_cast<int>(WiFi.status()), static_cast<unsigned>(ESP.getFreeHeap()));
      const bool ok = fetchFmMuf(muf);
      BootDiagnostics::printf("[BANDOPEN][FM_MUF] cycle result=%s value_mhz=%.1f heap=%u",
        ok ? "OK" : "FAILED", ok ? muf : 0.0f, static_cast<unsigned>(ESP.getFreeHeap()));
      Snapshot current = snapshot();
      current.fmMufStale = !ok;
      if (ok) { current.fmMufMHz = muf; current.fmMufValid = true; }
      publish(current);
    }
    if (wifiConnected_ && due) {
      const bool wasRequested = refreshRequested_;
      refreshRequested_ = false;
      lastAttemptMs_ = now;
      BootDiagnostics::printf("[BANDOPEN][HF] cycle start trigger=%s wifi=%d heap=%u",
        wasRequested ? "IMMEDIATE" : "SCHEDULED", static_cast<int>(WiFi.status()),
        static_cast<unsigned>(ESP.getFreeHeap()));
      Snapshot current = snapshot();
      current.fetching = true;
      current.fetchFailed = false;
      publish(current);
      Snapshot fresh;
      if (fetchConditions(fresh)) {
        fresh.valid = true;
        fresh.stale = false;
        fresh.fetching = false;
        fresh.waitingForWifi = false;
        fresh.fetchFailed = false;
        fresh.updatedAtMs = millis();
        // Preserve the independently refreshed FM-DX value when the HF feed
        // snapshot is replaced; the two endpoints have different cadences.
        const Snapshot latest = snapshot();
        fresh.fmMufMHz = latest.fmMufMHz;
        fresh.fmMufValid = latest.fmMufValid;
        fresh.fmMufStale = latest.fmMufStale;
        publish(fresh);
        BootDiagnostics::printf("[BANDOPEN][HF] publish SUCCESS source_updated=%s elapsed_ms=%lu valid=1 stale=0",
          fresh.sourceUpdated, static_cast<unsigned long>(millis() - now));
      } else {
        current = snapshot();
        current.fetching = false;
        current.fetchFailed = true;
        current.stale = true;
        publish(current);
        lastAttemptMs_ = millis() - kRefreshIntervalMs + kRetryIntervalMs;
        BootDiagnostics::printf("[BANDOPEN][HF] publish FAILED; retained_valid=%u retained_source=%s retry_in_ms=%lu",
          current.valid ? 1U : 0U, current.sourceUpdated,
          static_cast<unsigned long>(kRetryIntervalMs));
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool BandOpeningService::fetchFmMuf(float& value) {
  const uint32_t startedAt = millis();
  BootDiagnostics::write("[BANDOPEN][FM_MUF] HTTPS begin");
  WiFiClientSecure client;
  client.setTimeout(8000);
  client.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(client, kFmMufUrl)) {
    BootDiagnostics::write("[BANDOPEN][FM_MUF] http.begin FAILED");
    return false;
  }
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    BootDiagnostics::printf("[BANDOPEN][FM_MUF] GET failed http=%d error=%s elapsed_ms=%lu",
      status, http.errorToString(status).c_str(), static_cast<unsigned long>(millis() - startedAt));
    http.end();
    return false;
  }
  const int length = http.getSize();
  BootDiagnostics::printf("[BANDOPEN][FM_MUF] HTTP 200 content_length=%d", length);
  if (length > 8192) { http.end(); BootDiagnostics::write("[BANDOPEN][FM_MUF] response rejected: too large"); return false; }
  String body = http.getString();
  http.end();
  BootDiagnostics::printf("[BANDOPEN][FM_MUF] body_bytes=%u elapsed_ms=%lu",
    static_cast<unsigned>(body.length()), static_cast<unsigned long>(millis() - startedAt));
  if (body.isEmpty() || body.length() > 8192 || body.startsWith("<")) {
    BootDiagnostics::write("[BANDOPEN][FM_MUF] response body empty/oversize/HTML");
    return false;
  }
  JsonDocument doc;
  const DeserializationError parseError = deserializeJson(doc, body);
  if (parseError) {
    BootDiagnostics::printf("[BANDOPEN][FM_MUF] JSON parse FAILED: %s", parseError.c_str());
    return false;
  }
  JsonVariantConst field = doc["europe"]["max_frequency"];
  if (!field.is<const char*>() && !field.is<float>() && !field.is<double>() && !field.is<int>()) return false;
  const char* text = field.is<const char*>() ? field.as<const char*>() : nullptr;
  if (field.is<const char*>() && (!text || strcmp(text, "No data") == 0)) return false;
  const float parsed = text ? strtof(text, nullptr) : field.as<float>();
  if (!isfinite(parsed) || parsed < 30.0f || parsed > 300.0f) {
    BootDiagnostics::printf("[BANDOPEN][FM_MUF] value rejected parsed=%.2f", parsed);
    return false;
  }
  value = parsed;
  BootDiagnostics::printf("[BANDOPEN][FM_MUF] value accepted %.2f MHz", value);
  return true;
}

bool BandOpeningService::fetchConditions(Snapshot& result) {
  const uint32_t startedAt = millis();
  BootDiagnostics::printf("[BANDOPEN][HF] HTTPS begin url=MapForHAM wifi=%d heap=%u",
    static_cast<int>(WiFi.status()), static_cast<unsigned>(ESP.getFreeHeap()));
  WiFiClientSecure client;
  client.setTimeout(8000);
  client.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  http.setUserAgent("ATS-Mini-OS/1.0 (+https://github.com/firetvstick2k2-afk/Mini_OS)");
  const char* responseHeaders[] = {"Content-Type", "Server"};
  http.collectHeaders(responseHeaders, 2);
  if (!http.begin(client, kFeedUrl)) {
    BootDiagnostics::write("[BANDOPEN][HF] http.begin FAILED (TLS/URL/client setup)");
    return false;
  }
  http.addHeader("Accept", "application/json");
  BootDiagnostics::write("[BANDOPEN][HF] http.begin OK; GET started with JSON Accept and firmware User-Agent");
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    BootDiagnostics::printf("[BANDOPEN][HF] GET failed http=%d error=%s content_type=%s server=%s elapsed_ms=%lu",
      status, http.errorToString(status).c_str(), http.header("Content-Type").c_str(),
      http.header("Server").c_str(), static_cast<unsigned long>(millis() - startedAt));
    http.end();
    return false;
  }
  const int length = http.getSize();
  BootDiagnostics::printf("[BANDOPEN][HF] HTTP 200 content_length=%d", length);
  if (length > static_cast<int>(kMaxResponseBytes)) {
    http.end();
    BootDiagnostics::printf("[BANDOPEN][HF] response rejected too large length=%d limit=%u",
      length, static_cast<unsigned>(kMaxResponseBytes));
    return false;
  }
  String body = http.getString();
  http.end();
  BootDiagnostics::printf("[BANDOPEN][HF] body_bytes=%u elapsed_ms=%lu heap=%u",
    static_cast<unsigned>(body.length()), static_cast<unsigned long>(millis() - startedAt),
    static_cast<unsigned>(ESP.getFreeHeap()));
  if (body.isEmpty() || body.length() > kMaxResponseBytes) {
    BootDiagnostics::printf("[BANDOPEN][HF] response rejected empty_or_oversize bytes=%u",
      static_cast<unsigned>(body.length()));
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    BootDiagnostics::printf("[BANDOPEN][HF] JSON parse FAILED: %s", error.c_str());
    return false;
  }
  BootDiagnostics::printf("[BANDOPEN][HF] JSON parse OK document_bytes=%u",
    static_cast<unsigned>(body.length()));
  const char* updated = doc["updated"] | "";
  if (!updated[0] || !timestampIsFresh(updated)) {
    BootDiagnostics::printf("[BANDOPEN][HF] timestamp REJECTED value=%s system_epoch=%lld",
      updated, static_cast<long long>(time(nullptr)));
    return false;
  }
  BootDiagnostics::printf("[BANDOPEN][HF] timestamp accepted value=%s system_epoch=%lld",
    updated, static_cast<long long>(time(nullptr)));
  strncpy(result.sourceUpdated, updated, sizeof(result.sourceUpdated) - 1);
  result.sourceUpdated[sizeof(result.sourceUpdated) - 1] = '\0';
  JsonArrayConst details = doc["bands_detail"].as<JsonArrayConst>();
  if (details.isNull() || details.size() == 0) {
    BootDiagnostics::write("[BANDOPEN][HF] JSON has no bands_detail array");
    return false;
  }
  BootDiagnostics::printf("[BANDOPEN][HF] bands_detail entries=%u",
    static_cast<unsigned>(details.size()));

  uint8_t matchedBands = 0;
  for (uint8_t i = 0; i < kBandCount; ++i) {
    result.rows[i] = {};
    const uint16_t meters = kBands[i].bandMeters;
    if (!meters) continue;
    bool foundBand = false;
    for (JsonObjectConst item : details) {
      if ((item["band_m"] | 0) == meters) {
        foundBand = true;
        result.rows[i].day = parseStatus(item["day"]);
        result.rows[i].night = parseStatus(item["night"]);
        if (result.rows[i].day != Status::Unknown || result.rows[i].night != Status::Unknown)
          ++matchedBands;
        break;
      }
    }
    BootDiagnostics::printf("[BANDOPEN][HF][BAND] %s band_m=%u found=%u day=%s night=%s",
      kBands[i].name, static_cast<unsigned>(meters), foundBand ? 1U : 0U,
      statusText(result.rows[i].day), statusText(result.rows[i].night));
  }
  // At least one known amateur band must have a usable assessment.
  const bool any = matchedBands > 0;
  BootDiagnostics::printf("[BANDOPEN][HF] parsed=%u/10 source_updated=%s total_elapsed_ms=%lu result=%s",
    static_cast<unsigned>(matchedBands), updated, static_cast<unsigned long>(millis() - startedAt),
    any ? "VALID" : "NO_RATED_BANDS");
  return any;
}
