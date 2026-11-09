#pragma once
#include <Arduino.h>

// Layout-independent news snapshot. No HTTP, scrolling or drawing in this class.
// The future fetch worker publishes complete headlines; renderers only read them.
class NewsService {
public:
  static constexpr uint8_t MAX_HEADLINES = 12;
  static constexpr size_t MAX_HEADLINE_BYTES = 192;
  static constexpr uint8_t MAX_FEEDS = 4;
  static constexpr size_t MAX_URL_BYTES = 192;
  static constexpr uint16_t MIN_REFRESH_MINUTES = 5;
  static constexpr uint16_t MAX_REFRESH_MINUTES = 1440;

  struct Headline {
    char title[MAX_HEADLINE_BYTES] = {};
    uint8_t sourceIndex = 0;
  };
  struct Snapshot {
    Headline items[MAX_HEADLINES] = {};
    uint8_t count = 0;
    uint32_t fetchedAtMs = 0;
    bool hasData = false;
    bool stale = true;
  };
  struct Configuration {
    char urls[MAX_FEEDS][MAX_URL_BYTES] = {};
    uint16_t refreshMinutes = 15;
    bool enabled = false;
  };

  const Configuration& configuration() const { return configuration_; }
  const Snapshot& snapshot() const { return snapshot_; }
  void setConfiguration(const Configuration& config);
  void publish(const Snapshot& updated);
  void markStale() { snapshot_.stale = true; }
  static bool validFeedUrl(const char* url);
private:
  Configuration configuration_;
  Snapshot snapshot_;
};
