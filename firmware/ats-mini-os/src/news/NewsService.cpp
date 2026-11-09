#include "NewsService.h"
#include <string.h>

bool NewsService::validFeedUrl(const char* url) {
  if (!url || !*url) return false; // Empty slots are allowed.
  const size_t len = strnlen(url, MAX_URL_BYTES);
  if (len >= MAX_URL_BYTES) return false;
  // HTTPS only: do not allow plain HTTP or arbitrary schemes.
  if (strncmp(url, "https://", 8) != 0 || len <= 8) return false;
  for (size_t i = 0; i < len; ++i)
    if (static_cast<unsigned char>(url[i]) <= 32) return false;
  return true;
}

void NewsService::setConfiguration(const Configuration& config) {
  Configuration next;
  next.enabled = config.enabled;
  next.refreshMinutes = constrain(config.refreshMinutes,
                                  MIN_REFRESH_MINUTES, MAX_REFRESH_MINUTES);
  for (uint8_t i = 0; i < MAX_FEEDS; ++i) {
    if (!config.urls[i][0] || !validFeedUrl(config.urls[i])) continue;
    strncpy(next.urls[i], config.urls[i], MAX_URL_BYTES - 1);
  }
  configuration_ = next;
}

void NewsService::publish(const Snapshot& updated) {
  Snapshot next = updated;
  if (next.count > MAX_HEADLINES) next.count = MAX_HEADLINES;
  for (uint8_t i = 0; i < next.count; ++i) {
    next.items[i].title[MAX_HEADLINE_BYTES - 1] = '\0';
    if (next.items[i].sourceIndex >= MAX_FEEDS)
      next.items[i].sourceIndex = 0;
  }
  next.hasData = next.count != 0;
  next.stale = !next.hasData;
  snapshot_ = next;
}
