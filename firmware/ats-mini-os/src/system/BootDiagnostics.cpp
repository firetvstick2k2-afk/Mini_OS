#include "BootDiagnostics.h"
#include <LittleFS.h>
#include <FS.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace BootDiagnostics {
namespace {
File logFile;
SemaphoreHandle_t logMutex = nullptr;
bool ready = false;
constexpr char LOG_PATH[] = "/ats-mini-os-boot.log";
constexpr size_t MAX_LOG_BYTES = 131072;
}

void begin() {
  if (!logMutex) logMutex = xSemaphoreCreateMutex();
  if (!logMutex || !LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
    Serial.println("[BOOTLOG] LittleFS append mount unavailable; no format attempted");
    return;
  }
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(500)) != pdTRUE) return;
  File existing = LittleFS.open(LOG_PATH, "r");
  const size_t currentSize = existing ? existing.size() : 0;
  if (existing) existing.close();
  if (currentSize >= MAX_LOG_BYTES) {
    xSemaphoreGive(logMutex);
    Serial.println("[BOOTLOG] log reached 128 KiB limit; preserving existing file");
    return;
  }
  logFile = LittleFS.open(LOG_PATH, FILE_APPEND);
  ready = static_cast<bool>(logFile);
  if (ready) {
    logFile.printf("\n=== BOOT SESSION %lu ms ===\n", static_cast<unsigned long>(millis()));
    logFile.flush();
  }
  xSemaphoreGive(logMutex);
  Serial.printf("[BOOTLOG] %s: %s\n", ready ? "append ready" : "open failed", LOG_PATH);
}

void write(const char* message) {
  if (!message) return;
  Serial.println(message);
  if (!ready || !logMutex) return;
  if (xSemaphoreTake(logMutex, pdMS_TO_TICKS(250)) != pdTRUE) return;
  if (logFile && logFile.size() < MAX_LOG_BYTES) {
    logFile.printf("[%10lu] %s\n", static_cast<unsigned long>(millis()), message);
    logFile.flush();
  }
  xSemaphoreGive(logMutex);
}

void printf(const char* format, ...) {
  if (!format) return;
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  write(buffer);
}
}
