#include "WeatherService.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>

namespace {
constexpr char kWeatherNamespace[] = "weather";
constexpr uint32_t kRefreshIntervalMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kRetryIntervalMs = 60UL * 1000UL;
constexpr size_t kMaxJsonBytes = 18000;

int16_t roundedTemperature(JsonVariantConst value) {
  return value.isNull() ? 0 : static_cast<int16_t>(lroundf(value.as<float>()));
}
uint8_t boundedPercent(JsonVariantConst value) {
  if (value.isNull()) return 0;
  return static_cast<uint8_t>(constrain(value.as<int>(), 0, 100));
}
const char* weatherDescription(uint8_t code) {
  if (code == 0) return "Klar";
  if (code <= 3) return "Teilweise bewoelkt";
  if (code == 45 || code == 48) return "Nebel";
  if (code >= 51 && code <= 57) return "Nieselregen";
  if (code >= 61 && code <= 67) return "Regen";
  if (code >= 71 && code <= 77) return "Schnee";
  if (code >= 80 && code <= 82) return "Regenschauer";
  if (code >= 85 && code <= 86) return "Schneeschauer";
  if (code >= 95) return "Gewitter";
  return "Wetter";
}
void weekdayFromDate(const char* date, char* output, size_t capacity, uint8_t index) {
  static const char* const names[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  if (index == 0) { strlcpy(output, "Heute", capacity); return; }
  int year=0, month=0, day=0;
  if (!date || sscanf(date, "%4d-%2d-%2d", &year, &month, &day) != 3) {
    snprintf(output, capacity, "Tag %u", static_cast<unsigned>(index + 1)); return;
  }
  static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  year -= month < 3;
  const int weekday = (year + year/4 - year/100 + year/400 + offsets[month-1] + day) % 7;
  strlcpy(output, names[weekday], capacity);
}
}

bool WeatherService::begin() {
  loadSettings();
  Snapshot initial;
  publish(initial);
  const BaseType_t created = xTaskCreatePinnedToCore(taskEntry, "WeatherWorker", 12288, this, 1, &taskHandle_, 0);
  Serial.printf("[WEATHER] worker task %s; postal_code=%s\n", created == pdPASS ? "created" : "creation failed", postalCode_);
  return created == pdPASS;
}
void WeatherService::loadSettings() {
  Preferences preferences;
  if (preferences.begin(kWeatherNamespace, true, "settings")) {
    String saved = preferences.getString("postal", "48143");
    saved.trim();
    if (saved.length() == 5) saved.toCharArray(postalCode_, sizeof(postalCode_));
    preferences.end();
  }
}
bool WeatherService::setPostalCode(const char* value) {
  if (!value) return false;
  String code(value); code.trim();
  if (code.length() != 5) return false;
  for (size_t i=0; i<code.length(); ++i) if (code[i] < '0' || code[i] > '9') return false;
  Preferences preferences;
  if (!preferences.begin(kWeatherNamespace, false, "settings")) return false;
  const bool saved = preferences.putString("postal", code) > 0;
  preferences.end();
  if (!saved) return false;
  code.toCharArray(postalCode_, sizeof(postalCode_));
  requestRefresh();
  return true;
}
String WeatherService::postalCode() const { return String(postalCode_); }
WeatherService::Snapshot WeatherService::snapshot() const {
  Snapshot copy;
  portENTER_CRITICAL(&snapshotMux_); copy = snapshot_; portEXIT_CRITICAL(&snapshotMux_);
  return copy;
}
void WeatherService::publish(const Snapshot& value) {
  portENTER_CRITICAL(&snapshotMux_); snapshot_ = value; portEXIT_CRITICAL(&snapshotMux_);
}
void WeatherService::requestRefresh() { refreshRequested_ = true; }
void WeatherService::update(bool wifiConnected) { wifiConnected_ = wifiConnected; }
void WeatherService::taskEntry(void* context) { static_cast<WeatherService*>(context)->taskLoop(); }
void WeatherService::taskLoop() {
  for (;;) {
    const uint32_t now = millis();
    const bool due = refreshRequested_ || !lastAttemptMs_ || now - lastAttemptMs_ >= kRefreshIntervalMs;
    if (wifiConnected_ && due) {
      refreshRequested_ = false; lastAttemptMs_ = now;
      Snapshot current = snapshot(); current.fetching = true; publish(current);
      Snapshot fresh;
      if (fetchWeather(postalCode_, fresh)) {
        fresh.valid = true; fresh.stale = false; fresh.fetching = false; fresh.updatedAtMs = millis();
        publish(fresh);
        Serial.printf("[WEATHER] updated location=%s temperature=%d C\n", fresh.location, fresh.temperatureC);
      } else {
        current = snapshot(); current.fetching = false; current.stale = true; publish(current);
        Serial.println("[WEATHER] update failed; keeping last successful snapshot");
        lastAttemptMs_ = millis() - kRefreshIntervalMs + kRetryIntervalMs;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
bool WeatherService::fetchJson(const String& url, String& body) {
  WiFiClientSecure client; client.setTimeout(8000); client.useBuiltinCACertBundle();
  HTTPClient http; http.setTimeout(8000); http.setReuse(false);
  if (!http.begin(client, url)) return false;
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("[WEATHER] HTTP status=%d error=%s\n", status, http.errorToString(status).c_str());
    http.end(); return false;
  }
  const int length = http.getSize();
  if (length > static_cast<int>(kMaxJsonBytes)) {
    Serial.printf("[WEATHER] JSON too large: %d bytes\n", length); http.end(); return false;
  }
  body = http.getString(); http.end();
  return body.length() > 16 && body.length() <= kMaxJsonBytes;
}
bool WeatherService::fetchWeather(const char* postalCode, Snapshot& result) {
  String geoBody;
  const String geoUrl = String("https://geocoding-api.open-meteo.com/v1/search?name=") + postalCode + "&count=1&language=de&countryCode=DE&format=json";
  if (!fetchJson(geoUrl, geoBody)) return false;
  JsonDocument geo;
  if (deserializeJson(geo, geoBody)) { Serial.println("[WEATHER] geocoding JSON parse failed"); return false; }
  JsonArrayConst matches = geo["results"].as<JsonArrayConst>();
  if (matches.isNull() || matches.size() == 0) { Serial.printf("[WEATHER] postal code not found: %s\n", postalCode); return false; }
  JsonObjectConst place = matches[0];
  const double latitude = place["latitude"] | 0.0;
  const double longitude = place["longitude"] | 0.0;
  if (latitude == 0.0 && longitude == 0.0) return false;
  strlcpy(result.location, place["name"] | "Standort", sizeof(result.location));
  strlcpy(result.country, place["country"] | "Deutschland", sizeof(result.country));

  const String forecastUrl = String("https://api.open-meteo.com/v1/forecast?latitude=") + String(latitude, 4) +
    "&longitude=" + String(longitude, 4) +
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,weather_code,wind_speed_10m,surface_pressure"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max&forecast_days=5&timezone=auto";
  String forecastBody;
  if (!fetchJson(forecastUrl, forecastBody)) return false;
  JsonDocument forecast;
  if (deserializeJson(forecast, forecastBody)) { Serial.println("[WEATHER] forecast JSON parse failed"); return false; }
  JsonObjectConst current = forecast["current"];
  if (current.isNull()) return false;
  result.temperatureC = roundedTemperature(current["temperature_2m"]);
  result.feelsLikeC = roundedTemperature(current["apparent_temperature"]);
  result.humidity = boundedPercent(current["relative_humidity_2m"]);
  result.pressureHpa = static_cast<uint16_t>(constrain(current["surface_pressure"] | 0.0f, 0.0f, 1500.0f));
  result.windKmh = static_cast<uint16_t>(constrain(lroundf(current["wind_speed_10m"] | 0.0f), 0L, 500L));
  result.weatherCode = static_cast<uint8_t>(current["weather_code"] | 0);
  strlcpy(result.description, weatherDescription(result.weatherCode), sizeof(result.description));
  JsonObjectConst daily = forecast["daily"];
  JsonArrayConst dates = daily["time"].as<JsonArrayConst>();
  JsonArrayConst codes = daily["weather_code"].as<JsonArrayConst>();
  JsonArrayConst highs = daily["temperature_2m_max"].as<JsonArrayConst>();
  JsonArrayConst lows = daily["temperature_2m_min"].as<JsonArrayConst>();
  JsonArrayConst rain = daily["precipitation_probability_max"].as<JsonArrayConst>();
  if (dates.size() < FORECAST_DAYS || codes.size() < FORECAST_DAYS || highs.size() < FORECAST_DAYS || lows.size() < FORECAST_DAYS || rain.size() < FORECAST_DAYS) return false;
  result.precipitation = rain.size() ? boundedPercent(rain[0]) : 0;
  for (uint8_t i=0; i<FORECAST_DAYS; ++i) {
    result.days[i].weatherCode = static_cast<uint8_t>(codes[i] | 0);
    result.days[i].maximumC = roundedTemperature(highs[i]);
    result.days[i].minimumC = roundedTemperature(lows[i]);
    result.days[i].precipitationChance = boundedPercent(rain[i]);
    const String date = dates[i].as<String>();
    weekdayFromDate(date.c_str(), result.weekdays[i], sizeof(result.weekdays[i]), i);
  }
  return true;
}
