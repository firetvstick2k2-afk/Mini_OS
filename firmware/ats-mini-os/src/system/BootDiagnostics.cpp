#include "BootDiagnostics.h"
#include <LittleFS.h>
#include "../storage/StorageDiscovery.h"
#include <FS.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace BootDiagnostics {
namespace {
File logFile;
SemaphoreHandle_t logMutex = nullptr;
bool ready = false;
bool storageAvailable = false;
bool logEnabled = true;
constexpr char LOG_PATH[] = "/ats-mini-os-boot.log";
constexpr size_t MAX_LOG_BYTES = 512UL * 1024UL;
constexpr char PREVIOUS_LOG_PATH[] = "/ats-mini-os-boot.previous.log";

bool rotateLogLocked() {
  if (logFile) logFile.close();
  if (LittleFS.exists(PREVIOUS_LOG_PATH) && !LittleFS.remove(PREVIOUS_LOG_PATH)) {
    ready = false;
    Serial.println("[BOOTLOG] could not remove previous rotated log");
    return false;
  }
  if (LittleFS.exists(LOG_PATH) && !LittleFS.rename(LOG_PATH, PREVIOUS_LOG_PATH)) {
    ready = false;
    Serial.println("[BOOTLOG] could not rotate full log; preserving current file");
    return false;
  }
  logFile = LittleFS.open(LOG_PATH, "a");
  ready = static_cast<bool>(logFile);
  if (ready) {
    logFile.printf("\n=== LOG ROTATED; NEW SESSION %lu ms ===\n", static_cast<unsigned long>(millis()));
    logFile.flush();
    Serial.println("[BOOTLOG] full log rotated to /ats-mini-os-boot.previous.log");
  }
  return ready;
}

bool openLogLocked() {
  if (!storageAvailable) return false;
  if (logFile) logFile.close();
  File existing = LittleFS.open(LOG_PATH, "r");
  const size_t currentSize = existing ? existing.size() : 0;
  if (existing) existing.close();
  if (currentSize >= MAX_LOG_BYTES && !rotateLogLocked()) return false;
  logFile = LittleFS.open(LOG_PATH, "a");
  ready = static_cast<bool>(logFile);
  if (ready) {
    logFile.printf("\n=== BOOT SESSION %lu ms ===\n", static_cast<unsigned long>(millis()));
    logFile.flush();
  }
  return ready;
}
}

void begin(bool enabled) {
  if (!logMutex) logMutex = xSemaphoreCreateMutex();
  storageAvailable = StorageDiscovery::ready();
  logEnabled = enabled;
  if (!logMutex || !storageAvailable) {
    ready = false;
    Serial.println("[BOOTLOG] existing LittleFS mount unavailable; no mount/format attempted");
    return;
  }
  if (!logEnabled) {
    ready = false;
    Serial.println("[BOOTLOG] persistent logging disabled by settings");
    return;
  }
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  openLogLocked();
  xSemaphoreGive(logMutex);
  Serial.printf("[BOOTLOG] %s: %s (limit=%u bytes; previous=%s)\n",
    ready ? "append ready" : "open failed", LOG_PATH,
    static_cast<unsigned>(MAX_LOG_BYTES), PREVIOUS_LOG_PATH);
  if (ready) write("[BOOTLOG] persistent LittleFS diagnostics active");
}

void setEnabled(bool enabled) {
  if (logEnabled == enabled) return;
  logEnabled = enabled;
  if (!logMutex) return;
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  if (!logEnabled) {
    if (logFile) logFile.close();
    ready = false;
    Serial.println("[BOOTLOG] persistent logging disabled");
  } else {
    const bool opened = openLogLocked();
    Serial.printf("[BOOTLOG] persistent logging enabled: %s\n", opened ? "ready" : "unavailable");
  }
  xSemaphoreGive(logMutex);
}

bool enabled() { return logEnabled; }

void write(const char* message) {
  if (!message) return;
  Serial.println(message);
  if (!logEnabled || !ready || !logMutex) return;
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
  if (logEnabled && logFile) {
    if (logFile.size() >= MAX_LOG_BYTES && !rotateLogLocked()) {
      xSemaphoreGive(logMutex);
      return;
    }
    if (logFile) {
      logFile.printf("[%10lu] %s\n", static_cast<unsigned long>(millis()), message);
      logFile.flush();
    }
  }
  xSemaphoreGive(logMutex);
}

void printf(const char* format, ...) {
  if (!format) return;
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  write(buffer);
}
}
