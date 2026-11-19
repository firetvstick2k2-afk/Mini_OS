#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

// Serializes memory-heavy HTTP/HTTPS transactions across independent services.
class NetworkRequestCoordinator {
public:
  static bool begin();
  static bool acquire(TickType_t waitTicks = 0);
  static void release();
};

class NetworkRequestGuard {
public:
  explicit NetworkRequestGuard(TickType_t waitTicks = pdMS_TO_TICKS(1000))
    : acquired_(NetworkRequestCoordinator::acquire(waitTicks)) {}
  ~NetworkRequestGuard() { if (acquired_) NetworkRequestCoordinator::release(); }
  explicit operator bool() const { return acquired_; }
  NetworkRequestGuard(const NetworkRequestGuard&) = delete;
  NetworkRequestGuard& operator=(const NetworkRequestGuard&) = delete;
private:
  bool acquired_;
};
