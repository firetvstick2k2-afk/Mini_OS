#pragma once

#include <Arduino.h>

class WeatherService {
public:
  static constexpr size_t POSTAL_CODE_BYTES = 12;
  static constexpr uint8_t FORECAST_DAYS = 5;

  struct Day {
    int16_t maximumC = 0;
    int16_t minimumC = 0;
    uint8_t precipitationChance = 0;
    uint8_t weatherCode = 0;
  };

  struct Snapshot {
    char location[48] = {};
    char country[32] = {};
    char description[32] = {};
    int16_t temperatureC = 0;
    int16_t feelsLikeC = 0;
    uint8_t humidity = 0;
    uint8_t precipitation = 0;
    uint16_t pressureHpa = 0;
    uint16_t windKmh = 0;
    uint8_t weatherCode = 0;
    Day days[FORECAST_DAYS] = {};
    char weekdays[FORECAST_DAYS][8] = {};
    uint32_t updatedAtMs = 0;
    bool valid = false;
    bool stale = true;
    bool fetching = false;
  };

  bool begin();
  void update(bool wifiConnected);
  bool setPostalCode(const char* postalCode);
  void requestRefresh();
  String postalCode() const;
  Snapshot snapshot() const;

private:
  static void taskEntry(void* context);
  void taskLoop();
  bool fetchWeather(const char* postalCode, Snapshot& result);
  bool fetchJson(const String& url, String& body);
  void publish(const Snapshot& value);
  void loadSettings();

  char postalCode_[POSTAL_CODE_BYTES] = {};
  mutable portMUX_TYPE snapshotMux_ = portMUX_INITIALIZER_UNLOCKED;
  Snapshot snapshot_{};
  TaskHandle_t taskHandle_ = nullptr;
  volatile bool refreshRequested_ = false;
  volatile bool wifiConnected_ = false;
  uint32_t lastAttemptMs_ = 0;
};
