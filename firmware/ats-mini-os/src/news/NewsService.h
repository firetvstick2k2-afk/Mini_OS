#pragma once
#include <Arduino.h>

class NewsService {
public:
  static constexpr uint8_t MAX_HEADLINES=12;
  static constexpr size_t MAX_HEADLINE_BYTES=192;
  static constexpr uint8_t MAX_FEEDS=4;
  static constexpr size_t MAX_URL_BYTES=192;
  static constexpr uint16_t MIN_REFRESH_MINUTES=1;
  static constexpr uint16_t MAX_REFRESH_MINUTES=1440;
  struct Headline { char title[MAX_HEADLINE_BYTES]={}; uint8_t sourceIndex=0; };
  struct Snapshot { Headline items[MAX_HEADLINES]={}; uint8_t count=0; uint32_t fetchedAtMs=0; bool hasData=false; bool stale=true; };
  struct Configuration { char urls[MAX_FEEDS][MAX_URL_BYTES]={}; uint16_t refreshMinutes=1; bool enabled=false; };
  const Configuration& configuration() const { return configuration_; }
  Snapshot snapshot() const;
  void setConfiguration(const Configuration& config);
  void publish(const Snapshot& updated);
  void markStale();
  void begin();
  void update(bool wifiConnected);
  static bool validFeedUrl(const char* url);
private:
  friend void newsWorker(void*);
  Configuration configuration_{};
  Snapshot snapshot_{};
};
