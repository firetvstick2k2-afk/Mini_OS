#include "BandOpeningService.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include "../system/BootDiagnostics.h"

namespace {
constexpr char kFeedUrl[] = "https://www.mapforham.com/hf-propagation/data/solar.json";
constexpr uint32_t kRefreshIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kRetryIntervalMs = 60UL * 1000UL;
constexpr size_t kMaxResponseBytes = 16384;
constexpr uint16_t kBlue = 0x001F;

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
  const BaseType_t created = xTaskCreatePinnedToCore(taskEntry, "BandOpenWorker", 8192, this, 1, &taskHandle_, 0);
  BootDiagnostics::printf("[BANDOPEN] worker %s\n", created == pdPASS ? "created" : "creation failed");
  return created == pdPASS;
}
void BandOpeningService::update(bool wifiConnected) {
  wifiConnected_ = wifiConnected;
}
Snapshot BandOpeningService::snapshot() const {
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
    if (wifiConnected_ && due) {
      refreshRequested_ = false;
      lastAttemptMs_ = now;
      Snapshot current = snapshot();
      current.fetching = true;
      publish(current);
      Snapshot fresh;
      if (fetchConditions(fresh)) {
        fresh.valid = true;
        fresh.stale = false;
        fresh.fetching = false;
        fresh.updatedAtMs = millis();
        publish(fresh);
        BootDiagnostics::write("[BANDOPEN] HF conditions updated (Map For HAM)");
      } else {
        current = snapshot();
        current.fetching = false;
        current.stale = true;
        publish(current);
        lastAttemptMs_ = millis() - kRefreshIntervalMs + kRetryIntervalMs;
        BootDiagnostics::write("[BANDOPEN] fetch failed; retaining last data as stale");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
bool BandOpeningService::fetchConditions(Snapshot& result) {
  WiFiClientSecure client;
  client.setTimeout(8000);
  client.useBuiltinCACertBundle();
  HTTPClient http;
  http.setTimeout(8000);
  http.setReuse(false);
  if (!http.begin(client, kFeedUrl)) return false;
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    BootDiagnostics::printf("[BANDOPEN] HTTP status=%d\n", status);
    http.end();
    return false;
  }
  const int length = http.getSize();
  if (length > static_cast<int>(kMaxResponseBytes)) {
    http.end();
    BootDiagnostics::write("[BANDOPEN] response rejected: too large");
    return false;
  }
  String body = http.getString();
  http.end();
  if (body.isEmpty() || body.length() > kMaxResponseBytes) return false;

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, body);
  if (error) {
    BootDiagnostics::printf("[BANDOPEN] JSON parse failed: %s\n", error.c_str());
    return false;
  }
  const char* updated = doc["updated"] | "";
  if (!updated[0]) return false;
  JsonArrayConst details = doc["bands_detail"].as<JsonArrayConst>();
  if (details.isNull() || details.size() == 0) return false;

  for (uint8_t i = 0; i < kBandCount; ++i) {
    result.rows[i] = {};
    const uint16_t meters = kBands[i].bandMeters;
    if (!meters) continue;
    for (JsonObjectConst item : details) {
      if ((item["band_m"] | 0) == meters) {
        result.rows[i].day = parseStatus(item["day"]);
        result.rows[i].night = parseStatus(item["night"]);
        break;
      }
    }
  }
  // At least one known amateur band must have a usable assessment.
  bool any = false;
  for (uint8_t i = 0; i < kBandCount; ++i)
    any = any || result.rows[i].day != Status::Unknown || result.rows[i].night != Status::Unknown;
  return any;
}
