#include "WeatherService.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include "../system/BootDiagnostics.h"

namespace {
constexpr char kWeatherNamespace[] = "weather";
constexpr uint32_t kRefreshIntervalMs = 15UL * 60UL * 1000UL;
constexpr uint32_t kRetryIntervalMs = 60UL * 1000UL;
constexpr size_t kMaxJsonBytes = 48000;

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
  BootDiagnostics::write("[WEATHER] begin entered");
  loadSettings();
  Snapshot initial;
  publish(initial);
  const BaseType_t created = xTaskCreatePinnedToCore(taskEntry, "WeatherWorker", 12288, this, 1, &taskHandle_, 0);
  BootDiagnostics::printf("[WEATHER] worker task %s; postal_code='%s'\n",
    created == pdPASS ? "created" : "creation failed", postalCode_);
  return created == pdPASS;
}
void WeatherService::loadSettings() {
  Preferences preferences;
  const bool opened = preferences.begin(kWeatherNamespace, true, "settings");
  BootDiagnostics::printf("[WEATHER] load settings namespace='%s' opened=%s\n",
    kWeatherNamespace, opened ? "yes" : "no");
  if (!opened) return;
  String saved = preferences.getString("postal", "");
  saved.trim();
  if (saved.length() == 5) saved.toCharArray(postalCode_, sizeof(postalCode_));
  BootDiagnostics::printf("[WEATHER] stored postal='%s' length=%u valid=%s\n",
    saved.c_str(), static_cast<unsigned>(saved.length()),
    strlen(postalCode_) == 5 ? "yes" : "no");
  preferences.end();
}
bool WeatherService::setPostalCode(const char* value) {
  BootDiagnostics::printf("[WEATHER] setPostalCode called value='%s'\n", value ? value : "(null)");
  if (!value) return false;
  String code(value); code.trim();
  if (code.length() != 5) {
    BootDiagnostics::printf("[WEATHER] rejected postal: length=%u (expected 5)\n", static_cast<unsigned>(code.length()));
    return false;
  }
  for (size_t i=0; i<code.length(); ++i) {
    if (code[i] < '0' || code[i] > '9') {
      BootDiagnostics::printf("[WEATHER] rejected postal: non-digit at index=%u\n", static_cast<unsigned>(i));
      return false;
    }
  }
  Preferences preferences;
  if (!preferences.begin(kWeatherNamespace, false, "settings")) {
    BootDiagnostics::write("[WEATHER] ERROR: Preferences.begin for write failed");
    return false;
  }
  const size_t written = preferences.putString("postal", code);
  const String readBack = preferences.getString("postal", "");
  preferences.end();
  BootDiagnostics::printf("[WEATHER] save postal='%s' bytes_written=%u read_back='%s'\n",
    code.c_str(), static_cast<unsigned>(written), readBack.c_str());
  if (written == 0 || readBack != code) {
    BootDiagnostics::write("[WEATHER] ERROR: postal persistence verification failed");
    return false;
  }
  code.toCharArray(postalCode_, sizeof(postalCode_));
  requestRefresh();
  BootDiagnostics::printf("[WEATHER] postal accepted; refresh requested; wifi_connected=%s\n",
    wifiConnected_ ? "yes" : "no");
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
void WeatherService::update(bool wifiConnected) {
  static bool lastReported = false;
  static bool reportedOnce = false;
  wifiConnected_ = wifiConnected;
  if (!reportedOnce || wifiConnected != lastReported) {
    reportedOnce = true;
    lastReported = wifiConnected;
    BootDiagnostics::printf("[WEATHER] data-service WiFi=%s postal_configured=%s\n",
      wifiConnected ? "connected" : "disconnected",
      strlen(postalCode_) == 5 ? "yes" : "no");
  }
}
void WeatherService::taskEntry(void* context) { static_cast<WeatherService*>(context)->taskLoop(); }
void WeatherService::taskLoop() {
  BootDiagnostics::write("[WEATHER] worker task entered");
  bool waitingLogged = false;
  for (;;) {
    const uint32_t now = millis();
    const bool due = refreshRequested_ || !lastAttemptMs_ || now - lastAttemptMs_ >= kRefreshIntervalMs;
    if (strlen(postalCode_) != 5 && !waitingLogged) {
      BootDiagnostics::write("[WEATHER] waiting: no valid postal code configured");
      waitingLogged = true;
    }
    if (strlen(postalCode_) == 5) waitingLogged = false;
    if (!wifiConnected_ && strlen(postalCode_) == 5 && due) {
      static bool wifiWaitLogged = false;
      if (!wifiWaitLogged) {
        BootDiagnostics::write("[WEATHER] waiting: data-service WiFi is not connected");
        wifiWaitLogged = true;
      }
    }
    if (wifiConnected_ && due && strlen(postalCode_) == 5) {
      BootDiagnostics::printf("[WEATHER] refresh starting postal='%s' requested=%s\n",
        postalCode_, refreshRequested_ ? "yes" : "no");
      refreshRequested_ = false; lastAttemptMs_ = now;
      Snapshot current = snapshot(); current.fetching = true; publish(current);
      Snapshot fresh;
      if (fetchWeather(postalCode_, fresh)) {
        fresh.valid = true; fresh.stale = false; fresh.fetching = false; fresh.updatedAtMs = millis();
        publish(fresh);
        BootDiagnostics::printf("[WEATHER] updated location=%s temperature=%d C\n", fresh.location, fresh.temperatureC);
      } else {
        current = snapshot(); current.fetching = false; current.stale = true; publish(current);
        BootDiagnostics::write("[WEATHER] update failed; keeping last successful snapshot");
        lastAttemptMs_ = millis() - kRefreshIntervalMs + kRetryIntervalMs;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
bool WeatherService::fetchJson(const String& url, String& body) {
  const bool geocoding = url.indexOf("geocoding-api.open-meteo.com") >= 0;
  BootDiagnostics::printf("[WEATHER] HTTP request start type=%s\n", geocoding ? "geocoding" : "forecast");
  WiFiClientSecure client; client.setTimeout(8000); client.useBuiltinCACertBundle();
  HTTPClient http; http.setTimeout(8000); http.setReuse(false);
  if (!http.begin(client, url)) {
    BootDiagnostics::printf("[WEATHER] HTTP begin failed type=%s\n", geocoding ? "geocoding" : "forecast");
    return false;
  }
  const int status = http.GET();
  BootDiagnostics::printf("[WEATHER] HTTP response type=%s status=%d\n", geocoding ? "geocoding" : "forecast", status);
  if (status != HTTP_CODE_OK) {
    BootDiagnostics::printf("[WEATHER] HTTP status=%d error=%s\n", status, http.errorToString(status).c_str());
    http.end(); return false;
  }
  const int length = http.getSize();
  if (length > static_cast<int>(kMaxJsonBytes)) {
    BootDiagnostics::printf("[WEATHER] JSON too large: %d bytes\n", length); http.end(); return false;
  }
  body = http.getString(); http.end();
  BootDiagnostics::printf("[WEATHER] HTTP body type=%s bytes=%u\n",
    geocoding ? "geocoding" : "forecast", static_cast<unsigned>(body.length()));
  if (body.length() <= 16 || body.length() > kMaxJsonBytes) {
    BootDiagnostics::printf("[WEATHER] invalid body size type=%s bytes=%u\n",
      geocoding ? "geocoding" : "forecast", static_cast<unsigned>(body.length()));
    return false;
  }
  return true;
}
bool WeatherService::fetchWeather(const char* postalCode, Snapshot& result) {
  // Resolve German postcodes through the postal-code directory first. This
  // avoids relying on weather-provider geocoders accepting numeric postcodes.
  String localityBody;
  const String localityUrl = String("https://openplzapi.org/de/Localities?postalCode=") + postalCode + "&page=1&pageSize=5";
  if (!fetchJson(localityUrl, localityBody)) {
    BootDiagnostics::write("[WEATHER] OpenPLZ postcode lookup failed");
    return false;
  }

  JsonDocument localityDoc;
  if (deserializeJson(localityDoc, localityBody)) {
    BootDiagnostics::write("[WEATHER] OpenPLZ JSON parse failed");
    return false;
  }
  JsonArrayConst localities = localityDoc.as<JsonArrayConst>();
  if (localities.isNull() || localities.size() == 0) {
    BootDiagnostics::printf("[WEATHER] no locality found for postal code %s", postalCode);
    return false;
  }

  JsonObjectConst locality = localities[0];
  const char* localityName = locality["name"] | "";
  const char* municipalityName = locality["municipality"]["name"] | "";
  const char* stateName = locality["federalState"]["name"] | "";
  String searchName = localityName[0] ? String(localityName) : String(municipalityName);
  if (!searchName.length()) {
    BootDiagnostics::write("[WEATHER] OpenPLZ result has no locality name");
    return false;
  }
  BootDiagnostics::printf("[WEATHER] postcode resolved locality='%s' municipality='%s'",
    searchName.c_str(), municipalityName);

  // Open-Meteo is used only as a place-name geocoder here; all weather data
  // comes from Bright Sky / DWD.
  String geoBody;
  const String geoUrl = String("https://geocoding-api.open-meteo.com/v1/search?name=") +
    searchName + "&count=5&language=de&countryCode=DE&format=json";
  if (!fetchJson(geoUrl, geoBody)) return false;
  JsonDocument geo;
  if (deserializeJson(geo, geoBody)) {
    BootDiagnostics::write("[WEATHER] locality geocoding JSON parse failed");
    return false;
  }
  JsonArrayConst matches = geo["results"].as<JsonArrayConst>();
  if (matches.isNull() || matches.size() == 0) {
    BootDiagnostics::printf("[WEATHER] locality geocoding returned no coordinates for '%s'", searchName.c_str());
    return false;
  }

  JsonObjectConst place = matches[0];
  const double latitude = place["latitude"] | 0.0;
  const double longitude = place["longitude"] | 0.0;
  if (latitude == 0.0 && longitude == 0.0) {
    BootDiagnostics::write("[WEATHER] locality geocoding returned invalid coordinates");
    return false;
  }
  strlcpy(result.location, searchName.c_str(), sizeof(result.location));
  strlcpy(result.country, "Deutschland", sizeof(result.country));
  BootDiagnostics::printf("[WEATHER] location coordinates lat=%.4f lon=%.4f", latitude, longitude);

  // Current conditions are compiled by Bright Sky from recent DWD SYNOP data.
  const String locationArgs = String("?lat=") + String(latitude, 4) +
    "&lon=" + String(longitude, 4) + "&tz=Europe%2FBerlin";
  String currentBody;
  if (!fetchJson(String("https://api.brightsky.dev/current_weather") + locationArgs, currentBody)) return false;
  JsonDocument currentDoc;
  if (deserializeJson(currentDoc, currentBody)) {
    BootDiagnostics::write("[WEATHER] Bright Sky current-weather JSON parse failed");
    return false;
  }
  JsonObjectConst current = currentDoc["weather"];
  if (current.isNull()) {
    BootDiagnostics::write("[WEATHER] Bright Sky current-weather record missing");
    return false;
  }
  result.temperatureC = roundedTemperature(current["temperature"]);
  result.feelsLikeC = result.temperatureC;
  result.humidity = boundedPercent(current["relative_humidity"]);
  result.pressureHpa = static_cast<uint16_t>(constrain(current["pressure_msl"] | 0.0f, 0.0f, 1500.0f));
  result.windKmh = static_cast<uint16_t>(constrain(lroundf(current["wind_speed_10"] | 0.0f), 0L, 500L));
  const char* condition = current["condition"] | "dry";
  if (!strcmp(condition, "fog")) result.weatherCode = 45;
  else if (!strcmp(condition, "rain")) result.weatherCode = 61;
  else if (!strcmp(condition, "sleet")) result.weatherCode = 66;
  else if (!strcmp(condition, "snow")) result.weatherCode = 71;
  else if (!strcmp(condition, "hail")) result.weatherCode = 96;
  else if (!strcmp(condition, "thunderstorm")) result.weatherCode = 95;
  else result.weatherCode = 0;
  strlcpy(result.description, weatherDescription(result.weatherCode), sizeof(result.description));

  // Bright Sky's /weather endpoint returns hourly observations and forecasts.
  // Request six calendar days so the display can reliably fill five day slots.
  time_t now = time(nullptr);
  if (now < 1704067200) {
    BootDiagnostics::write("[WEATHER] system clock not synchronized; cannot request dated DWD forecast");
    return false;
  }
  struct tm dateParts;
  gmtime_r(&now, &dateParts);
  char firstDate[12] = {};
  strftime(firstDate, sizeof(firstDate), "%Y-%m-%d", &dateParts);
  time_t endTime = now + 5 * 24 * 60 * 60;
  struct tm endParts;
  gmtime_r(&endTime, &endParts);
  char lastDate[12] = {};
  strftime(lastDate, sizeof(lastDate), "%Y-%m-%d", &endParts);

  const String forecastUrl = String("https://api.brightsky.dev/weather") + locationArgs +
    "&date=" + firstDate + "&last_date=" + lastDate;
  String forecastBody;
  if (!fetchJson(forecastUrl, forecastBody)) return false;
  JsonDocument forecast;
  if (deserializeJson(forecast, forecastBody)) {
    BootDiagnostics::write("[WEATHER] Bright Sky forecast JSON parse failed");
    return false;
  }
  JsonArrayConst records = forecast["weather"].as<JsonArrayConst>();
  if (records.isNull() || records.size() == 0) {
    BootDiagnostics::write("[WEATHER] Bright Sky returned no hourly forecast records");
    return false;
  }

  static const char* const names[] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};
  int16_t highs[FORECAST_DAYS];
  int16_t lows[FORECAST_DAYS];
  uint8_t rain[FORECAST_DAYS] = {};
  uint8_t codes[FORECAST_DAYS] = {};
  bool found[FORECAST_DAYS] = {};
  for (uint8_t i = 0; i < FORECAST_DAYS; ++i) {
    highs[i] = INT16_MIN;
    lows[i] = INT16_MAX;
  }

  for (JsonObjectConst record : records) {
    const char* timestamp = record["timestamp"] | "";
    if (strlen(timestamp) < 10) continue;
    int year = 0, month = 0, day = 0;
    if (sscanf(timestamp, "%4d-%2d-%2d", &year, &month, &day) != 3) continue;
    struct tm recordDate = {};
    recordDate.tm_year = year - 1900;
    recordDate.tm_mon = month - 1;
    recordDate.tm_mday = day;
    const time_t recordDay = timegm(&recordDate);
    const time_t startDay = (now / 86400) * 86400;
    const int dayIndex = static_cast<int>((recordDay - startDay) / 86400);
    if (dayIndex < 0 || dayIndex >= FORECAST_DAYS) continue;

    const int16_t temperature = roundedTemperature(record["temperature"]);
    if (temperature > highs[dayIndex]) highs[dayIndex] = temperature;
    if (temperature < lows[dayIndex]) lows[dayIndex] = temperature;
    const uint8_t probability = boundedPercent(record["precipitation_probability"]);
    if (probability > rain[dayIndex]) rain[dayIndex] = probability;
    const char* icon = record["icon"] | "";
    uint8_t code = 0;
    if (strstr(icon, "thunderstorm")) code = 95;
    else if (strstr(icon, "snow")) code = 71;
    else if (strstr(icon, "sleet")) code = 66;
    else if (strstr(icon, "hail")) code = 96;
    else if (strstr(icon, "rain")) code = 61;
    else if (strstr(icon, "fog")) code = 45;
    else if (strstr(icon, "cloud")) code = 2;
    if (!found[dayIndex] || code > codes[dayIndex]) codes[dayIndex] = code;
    found[dayIndex] = true;
  }

  for (uint8_t i = 0; i < FORECAST_DAYS; ++i) {
    if (!found[i] || highs[i] == INT16_MIN || lows[i] == INT16_MAX) {
      BootDiagnostics::printf("[WEATHER] forecast day %u missing from Bright Sky response",
        static_cast<unsigned>(i + 1));
      return false;
    }
    result.days[i].maximumC = highs[i];
    result.days[i].minimumC = lows[i];
    result.days[i].precipitationChance = rain[i];
    result.days[i].weatherCode = codes[i];
    time_t dayTime = (now / 86400) * 86400 + static_cast<time_t>(i) * 86400;
    struct tm weekdayParts;
    gmtime_r(&dayTime, &weekdayParts);
    if (i == 0) strlcpy(result.weekdays[i], "Heute", sizeof(result.weekdays[i]));
    else strlcpy(result.weekdays[i], names[weekdayParts.tm_wday], sizeof(result.weekdays[i]));
  }
  result.precipitation = rain[0];
  BootDiagnostics::printf("[WEATHER] Bright Sky forecast parsed hourly_records=%u days=%u",
    static_cast<unsigned>(records.size()), static_cast<unsigned>(FORECAST_DAYS));
  return true;
}
