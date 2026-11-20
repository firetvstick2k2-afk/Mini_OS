#include "MemoryManager.h"

#include <Arduino.h>
#include <string.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "BootDiagnostics.h"

namespace MemoryManager {
namespace {
constexpr uint32_t REPORT_INTERVAL_MS = 15000;
constexpr size_t MODULE_SLOTS = 12;
constexpr size_t ALLOCATION_SLOTS = 64;
constexpr size_t OTHER_MODULE_SLOT = MODULE_SLOTS - 1;

struct ModuleRecord {
  char name[20];
  size_t currentBytes;
  size_t peakBytes;
  uint32_t liveAllocations;
  uint32_t totalSuccessfulAllocations;
  uint32_t allocationFailures;
  size_t currentInternalBytes;
  size_t peakInternalBytes;
  size_t currentPsramBytes;
  size_t peakPsramBytes;
  size_t currentOtherBytes;
  size_t peakOtherBytes;
};

struct AllocationRecord {
  void* pointer;
  size_t bytes;
  uint32_t capabilities;
  uint32_t allocatedAtMs;
  uint8_t moduleIndex;
  uint8_t region;
  bool active;
};

enum : uint8_t { REGION_INTERNAL = 0, REGION_PSRAM = 1, REGION_OTHER = 2 };

ModuleRecord modules[MODULE_SLOTS] = {};
AllocationRecord allocations[ALLOCATION_SLOTS] = {};
ModuleStats moduleSnapshot[MODULE_SLOTS] = {};
AllocationStats trackedStats{};
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

uint8_t moduleIndexLocked(const char* module) {
  const char* label = module && module[0] ? module : "unknown";
  for (size_t i = 0; i < OTHER_MODULE_SLOT; ++i) {
    if (modules[i].name[0] && strncmp(modules[i].name, label, sizeof(modules[i].name)) == 0)
      return static_cast<uint8_t>(i);
  }
  for (size_t i = 0; i < OTHER_MODULE_SLOT; ++i) {
    if (!modules[i].name[0]) {
      strncpy(modules[i].name, label, sizeof(modules[i].name) - 1);
      modules[i].name[sizeof(modules[i].name) - 1] = '\0';
      return static_cast<uint8_t>(i);
    }
  }
  if (!modules[OTHER_MODULE_SLOT].name[0])
    strncpy(modules[OTHER_MODULE_SLOT].name, "other", sizeof(modules[OTHER_MODULE_SLOT].name) - 1);
  return static_cast<uint8_t>(OTHER_MODULE_SLOT);
}

uint8_t regionForCapabilities(uint32_t capabilities) {
  const bool wantsPsram = (capabilities & MALLOC_CAP_SPIRAM) != 0;
  const bool wantsInternal = (capabilities & MALLOC_CAP_INTERNAL) != 0;
  if (wantsPsram && !wantsInternal) return REGION_PSRAM;
  if (wantsInternal && !wantsPsram) return REGION_INTERNAL;
  return REGION_OTHER;
}

void recordFailure(const char* module) {
  portENTER_CRITICAL(&statsMux);
  ++trackedStats.allocationFailures;
  const uint8_t index = moduleIndexLocked(module);
  ++modules[index].allocationFailures;
  portEXIT_CRITICAL(&statsMux);
}

void updatePeak(size_t current, size_t& peak) {
  if (current > peak) peak = current;
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
    "[MEM][%s][SYSTEM] PSRAM total=%u free=%u largest=%u min_free=%u frag=%u%% | "
    "SRAM total=%u free=%u largest=%u min_free=%u frag=%u%% | stack_hwm=%u",
    event ? event : "PERIODIC",
    static_cast<unsigned>(psramTotal), static_cast<unsigned>(psramFree),
    static_cast<unsigned>(psramLargest), static_cast<unsigned>(psramMinimum),
    static_cast<unsigned>(psramFragmentation),
    static_cast<unsigned>(internalTotal), static_cast<unsigned>(internalFree),
    static_cast<unsigned>(internalLargest), static_cast<unsigned>(internalMinimum),
    static_cast<unsigned>(internalFragmentation),
    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));

  AllocationStats snapshot;
  getStats(snapshot);
  BootDiagnostics::printf(
    "[MEM][%s][TRACKED] live=%lu total_ok=%lu current=%u peak=%u "
    "SRAM=%u/%u PSRAM=%u/%u OTHER=%u/%u failures=%lu track_overflow=%lu unknown_free=%lu",
    event ? event : "PERIODIC",
    static_cast<unsigned long>(snapshot.liveAllocations),
    static_cast<unsigned long>(snapshot.totalSuccessfulAllocations),
    static_cast<unsigned>(snapshot.currentBytes), static_cast<unsigned>(snapshot.peakBytes),
    static_cast<unsigned>(snapshot.currentInternalBytes), static_cast<unsigned>(snapshot.peakInternalBytes),
    static_cast<unsigned>(snapshot.currentPsramBytes), static_cast<unsigned>(snapshot.peakPsramBytes),
    static_cast<unsigned>(snapshot.currentOtherBytes), static_cast<unsigned>(snapshot.peakOtherBytes),
    static_cast<unsigned long>(snapshot.allocationFailures),
    static_cast<unsigned long>(snapshot.trackingOverflows),
    static_cast<unsigned long>(snapshot.untrackedFrees));

  const size_t count = getModuleStats(moduleSnapshot, MODULE_SLOTS);
  for (size_t i = 0; i < count; ++i) {
    if (!moduleSnapshot[i].liveAllocations && !moduleSnapshot[i].totalSuccessfulAllocations && !moduleSnapshot[i].allocationFailures) continue;
    BootDiagnostics::printf(
      "[MEM][MODULE] %s live=%lu current=%u peak=%u SRAM=%u/%u PSRAM=%u/%u OTHER=%u/%u ok=%lu fail=%lu",
      moduleSnapshot[i].name,
      static_cast<unsigned long>(moduleSnapshot[i].liveAllocations),
      static_cast<unsigned>(moduleSnapshot[i].currentBytes),
      static_cast<unsigned>(moduleSnapshot[i].peakBytes),
      static_cast<unsigned>(moduleSnapshot[i].currentInternalBytes),
      static_cast<unsigned>(moduleSnapshot[i].peakInternalBytes),
      static_cast<unsigned>(moduleSnapshot[i].currentPsramBytes),
      static_cast<unsigned>(moduleSnapshot[i].peakPsramBytes),
      static_cast<unsigned>(moduleSnapshot[i].currentOtherBytes),
      static_cast<unsigned>(moduleSnapshot[i].peakOtherBytes),
      static_cast<unsigned long>(moduleSnapshot[i].totalSuccessfulAllocations),
      static_cast<unsigned long>(moduleSnapshot[i].allocationFailures));
  }
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
    return nullptr;
  }

  const uint8_t region = regionForCapabilities(capabilities);
  bool recorded = false;
  portENTER_CRITICAL(&statsMux);
  size_t slot = ALLOCATION_SLOTS;
  for (size_t i = 0; i < ALLOCATION_SLOTS; ++i) {
    if (!allocations[i].active) { slot = i; break; }
  }
  if (slot < ALLOCATION_SLOTS) {
    const uint8_t moduleIndex = moduleIndexLocked(module);
    allocations[slot] = {pointer, bytes, capabilities, millis(), moduleIndex, region, true};
    ModuleRecord& moduleRecord = modules[moduleIndex];
    moduleRecord.currentBytes += bytes;
    updatePeak(moduleRecord.currentBytes, moduleRecord.peakBytes);
    ++moduleRecord.liveAllocations;
    ++moduleRecord.totalSuccessfulAllocations;
    trackedStats.currentBytes += bytes;
    updatePeak(trackedStats.currentBytes, trackedStats.peakBytes);
    ++trackedStats.liveAllocations;
    ++trackedStats.totalSuccessfulAllocations;
    if (region == REGION_INTERNAL) {
      trackedStats.currentInternalBytes += bytes;
      updatePeak(trackedStats.currentInternalBytes, trackedStats.peakInternalBytes);
      moduleRecord.currentInternalBytes += bytes;
      updatePeak(moduleRecord.currentInternalBytes, moduleRecord.peakInternalBytes);
    } else if (region == REGION_PSRAM) {
      trackedStats.currentPsramBytes += bytes;
      updatePeak(trackedStats.currentPsramBytes, trackedStats.peakPsramBytes);
      moduleRecord.currentPsramBytes += bytes;
      updatePeak(moduleRecord.currentPsramBytes, moduleRecord.peakPsramBytes);
    } else {
      trackedStats.currentOtherBytes += bytes;
      updatePeak(trackedStats.currentOtherBytes, trackedStats.peakOtherBytes);
      moduleRecord.currentOtherBytes += bytes;
      updatePeak(moduleRecord.currentOtherBytes, moduleRecord.peakOtherBytes);
    }
    recorded = true;
  } else {
    ++trackedStats.trackingOverflows;
  }
  portEXIT_CRITICAL(&statsMux);

  if (!recorded) {
    heap_caps_free(pointer);
    recordFailure(module);
    BootDiagnostics::printf("[MEM][TRACK_OVERFLOW] module=%s slots=%u; allocation rejected",
      module && module[0] ? module : "unknown", static_cast<unsigned>(ALLOCATION_SLOTS));
    return nullptr;
  }
  return pointer;
}

void deallocate(void* pointer) {
  if (!pointer) return;
  bool tracked = false;
  portENTER_CRITICAL(&statsMux);
  for (size_t i = 0; i < ALLOCATION_SLOTS; ++i) {
    AllocationRecord& allocation = allocations[i];
    if (!allocation.active || allocation.pointer != pointer) continue;
    const size_t bytes = allocation.bytes;
    const uint8_t region = allocation.region;
    const uint8_t moduleIndex = allocation.moduleIndex;
    allocation = {};
    if (trackedStats.currentBytes >= bytes) trackedStats.currentBytes -= bytes;
    if (trackedStats.liveAllocations) --trackedStats.liveAllocations;
    ModuleRecord& moduleRecord = modules[moduleIndex];
    if (moduleRecord.currentBytes >= bytes) moduleRecord.currentBytes -= bytes;
    if (moduleRecord.liveAllocations) --moduleRecord.liveAllocations;
    if (region == REGION_INTERNAL) {
      if (trackedStats.currentInternalBytes >= bytes) trackedStats.currentInternalBytes -= bytes;
      if (moduleRecord.currentInternalBytes >= bytes) moduleRecord.currentInternalBytes -= bytes;
    } else if (region == REGION_PSRAM) {
      if (trackedStats.currentPsramBytes >= bytes) trackedStats.currentPsramBytes -= bytes;
      if (moduleRecord.currentPsramBytes >= bytes) moduleRecord.currentPsramBytes -= bytes;
    } else {
      if (trackedStats.currentOtherBytes >= bytes) trackedStats.currentOtherBytes -= bytes;
      if (moduleRecord.currentOtherBytes >= bytes) moduleRecord.currentOtherBytes -= bytes;
    }
    tracked = true;
    break;
  }
  if (!tracked) ++trackedStats.untrackedFrees;
  portEXIT_CRITICAL(&statsMux);
  heap_caps_free(pointer);
}

uint32_t allocationFailures() {
  portENTER_CRITICAL(&statsMux);
  const uint32_t value = trackedStats.allocationFailures;
  portEXIT_CRITICAL(&statsMux);
  return value;
}

void getStats(AllocationStats& out) {
  portENTER_CRITICAL(&statsMux);
  out = trackedStats;
  portEXIT_CRITICAL(&statsMux);
}

size_t getModuleStats(ModuleStats* out, size_t capacity) {
  if (!out || !capacity) return 0;
  size_t copied = 0;
  portENTER_CRITICAL(&statsMux);
  for (size_t i = 0; i < MODULE_SLOTS && copied < capacity; ++i) {
    if (!modules[i].name[0]) continue;
    ModuleStats& target = out[copied++];
    strncpy(target.name, modules[i].name, sizeof(target.name) - 1);
    target.name[sizeof(target.name) - 1] = '\0';
    target.currentBytes = modules[i].currentBytes;
    target.peakBytes = modules[i].peakBytes;
    target.currentInternalBytes = modules[i].currentInternalBytes;
    target.peakInternalBytes = modules[i].peakInternalBytes;
    target.currentPsramBytes = modules[i].currentPsramBytes;
    target.peakPsramBytes = modules[i].peakPsramBytes;
    target.currentOtherBytes = modules[i].currentOtherBytes;
    target.peakOtherBytes = modules[i].peakOtherBytes;
    target.liveAllocations = modules[i].liveAllocations;
    target.totalSuccessfulAllocations = modules[i].totalSuccessfulAllocations;
    target.allocationFailures = modules[i].allocationFailures;
  }
  portEXIT_CRITICAL(&statsMux);
  return copied;
}

size_t getLiveAllocations(TrackedAllocation* out, size_t capacity) {
  if (!out || !capacity) return 0;
  size_t copied = 0;
  const uint32_t now = millis();
  portENTER_CRITICAL(&statsMux);
  for (size_t i = 0; i < ALLOCATION_SLOTS && copied < capacity; ++i) {
    const AllocationRecord& allocation = allocations[i];
    if (!allocation.active) continue;
    TrackedAllocation& target = out[copied++];
    target.address = reinterpret_cast<uintptr_t>(allocation.pointer);
    target.bytes = allocation.bytes;
    target.capabilities = allocation.capabilities;
    target.ageMs = now - allocation.allocatedAtMs;
    const char* module = modules[allocation.moduleIndex].name;
    strncpy(target.module, module, sizeof(target.module) - 1);
    target.module[sizeof(target.module) - 1] = '\0';
  }
  portEXIT_CRITICAL(&statsMux);
  return copied;
}

}
