#include "MemoryManager.h"

#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "BootDiagnostics.h"

namespace MemoryManager {
namespace {
constexpr uint32_t REPORT_INTERVAL_MS = 15000;
constexpr size_t MODULE_SLOTS = 8;

struct ModuleFailures {
  char name[20];
  uint32_t count;
};

ModuleFailures failures[MODULE_SLOTS] = {};
uint32_t totalFailures = 0;
uint32_t lastReportMs = 0;
bool started = false;
portMUX_TYPE statsMux = portMUX_INITIALIZER_UNLOCKED;

const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXTERNAL";
    case ESP_RST_SW: return "SOFTWARE";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

void recordFailure(const char* module) {
  portENTER_CRITICAL(&statsMux);
  ++totalFailures;
  size_t slot = MODULE_SLOTS;
  size_t empty = MODULE_SLOTS;
  for (size_t i = 0; i < MODULE_SLOTS; ++i) {
    if (failures[i].name[0] && module &&
        strncmp(failures[i].name, module, sizeof(failures[i].name)) == 0) {
      slot = i;
      break;
    }
    if (!failures[i].name[0] && empty == MODULE_SLOTS) empty = i;
  }
  if (slot == MODULE_SLOTS) slot = empty;
  if (slot < MODULE_SLOTS) {
    if (!failures[slot].name[0]) {
      const char* label = module && module[0] ? module : "unknown";
      strncpy(failures[slot].name, label, sizeof(failures[slot].name) - 1);
      failures[slot].name[sizeof(failures[slot].name) - 1] = '\0';
    }
    ++failures[slot].count;
  }
  portEXIT_CRITICAL(&statsMux);
}

void report(const char* event) {
  const uint32_t psramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
  const uint32_t internalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

  const size_t psramTotal = heap_caps_get_total_size(psramCaps);
  const size_t psramFree = heap_caps_get_free_size(psramCaps);
  const size_t psramLargest = heap_caps_get_largest_free_block(psramCaps);
  const size_t psramMinimum = heap_caps_get_minimum_free_size(psramCaps);

  const size_t internalTotal = heap_caps_get_total_size(internalCaps);
  const size_t internalFree = heap_caps_get_free_size(internalCaps);
  const size_t internalLargest = heap_caps_get_largest_free_block(internalCaps);
  const size_t internalMinimum = heap_caps_get_minimum_free_size(internalCaps);

  const uint32_t psramFragmentation = psramFree
    ? static_cast<uint32_t>(100U - (psramLargest * 100U / psramFree)) : 0U;
  const uint32_t internalFragmentation = internalFree
    ? static_cast<uint32_t>(100U - (internalLargest * 100U / internalFree)) : 0U;

  BootDiagnostics::printf(
    "[MEM][%s] PSRAM total=%u free=%u largest=%u min_free=%u frag=%u%% | "
    "SRAM total=%u free=%u largest=%u min_free=%u frag=%u%% | "
    "alloc_failures=%lu stack_hwm=%u",
    event ? event : "PERIODIC",
    static_cast<unsigned>(psramTotal), static_cast<unsigned>(psramFree),
    static_cast<unsigned>(psramLargest), static_cast<unsigned>(psramMinimum),
    static_cast<unsigned>(psramFragmentation),
    static_cast<unsigned>(internalTotal), static_cast<unsigned>(internalFree),
    static_cast<unsigned>(internalLargest), static_cast<unsigned>(internalMinimum),
    static_cast<unsigned>(internalFragmentation),
    static_cast<unsigned long>(allocationFailures()),
    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}
}

void begin() {
  if (started) return;
  started = true;
  lastReportMs = millis();
  BootDiagnostics::printf("[MEM][BOOT] reset_reason=%s (%d)",
    resetReasonName(esp_reset_reason()), static_cast<int>(esp_reset_reason()));
  report("START");
}

void update() {
  if (!started) return;
  const uint32_t now = millis();
  if (now - lastReportMs < REPORT_INTERVAL_MS) return;
  lastReportMs = now;
  report("15S");
}

void logStage(const char* stage) {
  if (!started) return;
  report(stage && stage[0] ? stage : "STAGE");
}

void* allocate(size_t bytes, uint32_t capabilities, const char* module) {
  if (!bytes) return nullptr;
  void* pointer = heap_caps_malloc(bytes, capabilities);
  if (!pointer) {
    recordFailure(module);
    BootDiagnostics::printf("[MEM][ALLOC_FAIL] module=%s bytes=%u caps=0x%08lX total_failures=%lu",
      module && module[0] ? module : "unknown",
      static_cast<unsigned>(bytes),
      static_cast<unsigned long>(capabilities),
      static_cast<unsigned long>(allocationFailures()));
  }
  return pointer;
}

void deallocate(void* pointer) {
  if (pointer) heap_caps_free(pointer);
}

uint32_t allocationFailures() {
  portENTER_CRITICAL(&statsMux);
  const uint32_t value = totalFailures;
  portEXIT_CRITICAL(&statsMux);
  return value;
}
}
