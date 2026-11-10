#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "../news/NewsService.h"
#include "../weather/WeatherService.h"

class WifiClient {
public:
  enum class Mode : uint8_t { OFF = 0, AP_ONLY = 1, AP_CONNECT = 2, CONNECT = 3, SYNC_ONLY = 4 };

  void begin();
  bool setMode(Mode mode);
  Mode mode() const { return mode_; }
  bool dataServicesAllowed() const { return mode_ == Mode::AP_CONNECT || mode_ == Mode::CONNECT; }
  String networkName() const;
  String ipAddress() const;
  void setNewsService(NewsService* service);
  void setWeatherService(WeatherService* service);
  void update();
  bool connected() const;
  bool setupAccessPointActive() const { return apActive_; }

private:
  static constexpr uint8_t NETWORK_COUNT = 3;
  static constexpr uint32_t RETRY_INTERVAL_MS = 12000;
  WebServer server_{80};
  Preferences preferences_;
  char ssid_[NETWORK_COUNT][33] = {};
  char password_[NETWORK_COUNT][65] = {};
  NewsService::Configuration newsConfig_;
  NewsService* newsService_ = nullptr;
  WeatherService* weatherService_ = nullptr;
  uint8_t activeNetwork_ = 0;
  wl_status_t lastStatus_ = WL_IDLE_STATUS;
  uint32_t lastLogMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
  bool preferencesReady_ = false;
  bool serverStarted_ = false;
  bool apActive_ = false;
  Mode mode_ = Mode::AP_CONNECT;

  bool stationEnabled() const;
  bool accessPointEnabled() const;
  void applyMode();

  void loadNetworks();
  bool saveNetworks();
  void connectNextNetwork();
  void startWebServer();
  void handleRoot();
  void handleSave();
  void handleStatus();
  void handleNewsSave();
  void handleWeatherSave();
  void loadNewsConfig();
  static String htmlEscape(const String& value);
};
