#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "../news/NewsService.h"

class WifiClient {
public:
  void begin();
  void setNewsService(NewsService* service);
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
  uint8_t activeNetwork_ = 0;
  wl_status_t lastStatus_ = WL_IDLE_STATUS;
  uint32_t lastLogMs_ = 0;
  uint32_t lastAttemptMs_ = 0;
  bool preferencesReady_ = false;
  bool serverStarted_ = false;
  bool apActive_ = false;

  void loadNetworks();
  bool saveNetworks();
  void connectNextNetwork();
  void startWebServer();
  void handleRoot();
  void handleSave();
  void handleStatus();
  void handleNewsSave();
  void loadNewsConfig();
  static String htmlEscape(const String& value);
};
