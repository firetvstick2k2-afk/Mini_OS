#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_heap_caps.h"

// Central, read-only heap monitor plus opt-in allocation wrappers.
// Existing allocations are observed in aggregate; use allocate()/deallocate()
// for per-module allocation-failure attribution.
namespace MemoryManager {
  void begin();
  void update();
  void logStage(const char* stage);

  void* allocate(size_t bytes, uint32_t capabilities, const char* module);
  void deallocate(void* pointer);

  uint32_t allocationFailures();
}
