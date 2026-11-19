#include "NetworkRequestCoordinator.h"
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t requestMutex = nullptr;
}

bool NetworkRequestCoordinator::begin() {
  if (requestMutex) return true;
  requestMutex = xSemaphoreCreateMutex();
  return requestMutex != nullptr;
}

bool NetworkRequestCoordinator::acquire(TickType_t waitTicks) {
  return requestMutex && xSemaphoreTake(requestMutex, waitTicks) == pdTRUE;
}

void NetworkRequestCoordinator::release() {
  if (requestMutex && xSemaphoreGetMutexHolder(requestMutex) == xTaskGetCurrentTaskHandle())
    xSemaphoreGive(requestMutex);
}
