#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_heap_caps.h"

namespace MemoryManager {
  struct AllocationStats {
    size_t currentBytes = 0;
    size_t peakBytes = 0;
    size_t currentInternalBytes = 0;
    size_t peakInternalBytes = 0;
    size_t currentPsramBytes = 0;
    size_t peakPsramBytes = 0;
    size_t currentOtherBytes = 0;
    size_t peakOtherBytes = 0;
    uint32_t liveAllocations = 0;
    uint32_t totalSuccessfulAllocations = 0;
    uint32_t allocationFailures = 0;
    uint32_t trackingOverflows = 0;
    uint32_t untrackedFrees = 0;
  };

  struct ModuleStats {
    char name[20] = {};
    size_t currentBytes = 0;
    size_t peakBytes = 0;
    size_t currentInternalBytes = 0;
    size_t peakInternalBytes = 0;
    size_t currentPsramBytes = 0;
    size_t peakPsramBytes = 0;
    size_t currentOtherBytes = 0;
    size_t peakOtherBytes = 0;
    uint32_t liveAllocations = 0;
    uint32_t totalSuccessfulAllocations = 0;
    uint32_t allocationFailures = 0;
  };

  struct TrackedAllocation {
    uintptr_t address = 0;
    size_t bytes = 0;
    uint32_t capabilities = 0;
    uint32_t ageMs = 0;
    char module[20] = {};
  };

  void begin();
  void update();
  void logStage(const char* stage);
  void* allocate(size_t bytes, uint32_t capabilities, const char* module);
  void deallocate(void* pointer);
  uint32_t allocationFailures();
  void getStats(AllocationStats& out);
  size_t getModuleStats(ModuleStats* out, size_t capacity);
  size_t getLiveAllocations(TrackedAllocation* out, size_t capacity);
}
