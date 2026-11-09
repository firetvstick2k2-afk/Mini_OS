#include "WifiClient.h"
#include "../news/NewsService.h"

namespace {
constexpr char SETTINGS_NAMESPACE[] = "network";
constexpr char SETUP_AP_SSID[] = "ATS-Mini-OS-Setup";
const IPAddress SETUP_IP(10, 1, 1, 1);
const IPAddress SETUP_GATEWAY(10, 1, 1, 1);
const IPAddress SETUP_SUBNET(255, 255, 255, 0);
}

void WifiClient::setNewsService(NewsService* service) {
  newsService_ = service;
  if (newsService_) newsService_->setConfiguration(newsConfig_);
}

bool WifiClient::stationEnabled() const {
  return mode_ == Mode::AP_CONNECT || mode_ == Mode::CONNECT || mode_ == Mode::SYNC_ONLY;
}

bool WifiClient::accessPointEnabled() const {
  return mode_ == Mode::AP_ONLY || mode_ == Mode::AP_CONNECT;
}

void WifiClient::begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  preferencesReady_ = preferences_.begin(SETTINGS_NAMESPACE, false, "settings");
  if (preferencesReady_) {
    loadNetworks();
    loadNewsConfig();
    const uint8_t savedMode = preferences_.getUChar("wmode", static_cast<uint8_t>(Mode::AP_CONNECT));
    mode_ = savedMode <= static_cast<uint8_t>(Mode::SYNC_ONLY)
      ? static_cast<Mode>(savedMode) : Mode::AP_CONNECT;
  }
  if (newsService_) newsService_->setConfiguration(newsConfig_);
  startWebServer();
  applyMode();
}

void WifiClient::applyMode() {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(false, false);
  apActive_ = false;
  if (mode_ == Mode::OFF) {
    WiFi.mode(WIFI_OFF);
    lastAttemptMs_ = millis();
    Serial.println("[WIFI] mode OFF: station and setup access point disabled");
    return;
  }
  if (accessPointEnabled() && stationEnabled()) WiFi.mode(WIFI_AP_STA);
  else if (accessPointEnabled()) WiFi.mode(WIFI_AP);
  else WiFi.mode(WIFI_STA);

  if (accessPointEnabled()) {
    WiFi.softAPConfig(SETUP_IP, SETUP_GATEWAY, SETUP_SUBNET);
    apActive_ = WiFi.softAP(SETUP_AP_SSID);
  }
  if (stationEnabled()) {
    bool found = false;
    for (uint8_t i = 0; i < NETWORK_COUNT; ++i) {
      if (ssid_[i][0]) { activeNetwork_ = i; found = true; break; }
    }
    if (found) {
      WiFi.begin(ssid_[activeNetwork_], password_[activeNetwork_]);
      lastAttemptMs_ = millis();
      Serial.printf("[WIFI] connecting to saved network slot %u\\n",
                    static_cast<unsigned>(activeNetwork_ + 1));
    } else Serial.println("[WIFI] no saved router network");
  }
  Serial.printf("[WIFI] mode=%u AP=%s STA=%s\\n",
    static_cast<unsigned>(mode_), apActive_ ? "ON" : "OFF",
    stationEnabled() ? "ON" : "OFF");
}

bool WifiClient::setMode(Mode mode) {
  if (static_cast<uint8_t>(mode) > static_cast<uint8_t>(Mode::SYNC_ONLY)) return false;
  if (mode_ == mode) return true;
  if (preferencesReady_ &&
      preferences_.putUChar("wmode", static_cast<uint8_t>(mode)) != sizeof(uint8_t)) return false;
  mode_ = mode;
  applyMode();
  return true;
}

String WifiClient::networkName() const {
  if (connected()) return WiFi.SSID();
  if (apActive_) return String(SETUP_AP_SSID);
  return String();
}

String WifiClient::ipAddress() const {
  if (connected()) return WiFi.localIP().toString();
  if (apActive_) return WiFi.softAPIP().toString();
  return String();
}

void WifiClient::loadNetworks() {
  for (uint8_t i = 0; i < NETWORK_COUNT; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "ssid%u", static_cast<unsigned>(i + 1));
    String ssid = preferences_.getString(key, "");
    snprintf(key, sizeof(key), "pass%u", static_cast<unsigned>(i + 1));
    String password = preferences_.getString(key, "");
    ssid.toCharArray(ssid_[i], sizeof(ssid_[i]));
    password.toCharArray(password_[i], sizeof(password_[i]));
  }
}

bool WifiClient::saveNetworks() {
  if (!preferencesReady_) return false;
  bool ok = true;
  for (uint8_t i = 0; i < NETWORK_COUNT; ++i) {
    char key[12];
    snprintf(key, sizeof(key), "ssid%u", static_cast<unsigned>(i + 1));
    ok &= preferences_.putString(key, ssid_[i]) > 0 || ssid_[i][0] == '\0';
    snprintf(key, sizeof(key), "pass%u", static_cast<unsigned>(i + 1));
    ok &= preferences_.putString(key, password_[i]) > 0 || password_[i][0] == '\0';
  }
  return ok;
}


void WifiClient::loadNewsConfig() {
  if (!preferencesReady_) return;
  newsConfig_.enabled = preferences_.getBool("newson", true);
  newsConfig_.refreshMinutes = preferences_.getUShort("nmin", 1);
  if (newsConfig_.refreshMinutes < 1 || newsConfig_.refreshMinutes > 1440)
    newsConfig_.refreshMinutes = 1;
  for (uint8_t i = 0; i < NewsService::MAX_FEEDS; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "nurl%u", static_cast<unsigned>(i + 1));
    String url = preferences_.getString(key, "");
    url.toCharArray(newsConfig_.urls[i], sizeof(newsConfig_.urls[i]));
    if (!NewsService::validFeedUrl(newsConfig_.urls[i]))
      newsConfig_.urls[i][0] = '\0';
  }
  if (!newsConfig_.urls[0][0]) strlcpy(newsConfig_.urls[0], "https://www.n-tv.de/rss", sizeof(newsConfig_.urls[0]));
}

void WifiClient::startWebServer() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/wifi/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/wifi/status", HTTP_GET, [this]() { handleStatus(); });
  server_.on("/news/save", HTTP_POST, [this]() { handleNewsSave(); });
  server_.onNotFound([this]() { server_.send(404, "text/plain", "Not found"); });
  server_.begin();
  serverStarted_ = true;
}

String WifiClient::htmlEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '&') out += F("&amp;");
    else if (c == '<') out += F("&lt;");
    else if (c == '>') out += F("&gt;");
    else if (c == '"') out += F("&quot;");
    else if (c == '\'') out += F("&#39;");
    else out += c;
  }
  return out;
}

void WifiClient::handleRoot() {
  String page;
  page.reserve(6500);
  page += F("<!doctype html><html lang='de'><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta charset='utf-8'><title>ATS Mini OS - WLAN</title><style>"
            "body{font:16px Arial,sans-serif;background:#080d18;color:#eef4ff;margin:0;padding:18px}"
            "main{max-width:560px;margin:auto}h1{color:#65e6c4}section{background:#101d32;border:1px solid #176b83;"
            "padding:14px;margin:14px 0;border-radius:8px}label{display:block;margin:10px 0 4px;color:#a8c9d8}"
            "input{box-sizing:border-box;width:100%;padding:11px;border:1px solid #426276;border-radius:4px;"
            "background:#07111d;color:white}button{padding:12px 18px;background:#087e91;color:white;border:0;"
            "border-radius:4px;font-weight:bold}small{color:#9bb2c2}</style></head><body><main>"
            "<h1>ATS Mini OS</h1><h2>WLAN-Einrichtung</h2>"
            "<p>Bis zu drei Netzwerke speichern. Das erste verfügbare Netzwerk wird verwendet.</p>");
  for (uint8_t i = 0; i < NETWORK_COUNT; ++i) {
    page += F("<section><h3>WLAN Netzwerk ");
    page += String(i + 1);
    page += F("</h3><label>SSID</label><input name='ssid");
    page += String(i + 1);
    page += F("' form='wifi' maxlength='32' value='");
    page += htmlEscape(String(ssid_[i]));
    page += F("'><label>Passwort</label><input type='password' name='pass");
    page += String(i + 1);
    page += F("' form='wifi' maxlength='63' placeholder='Leer lassen, um das gespeicherte Passwort zu behalten'>");
    page += F("</section>");
  }
  page += F("<section><h2>News-Ticker</h2><p>Eigene RSS- oder Atom-Feeds eintragen. Aktualisierung nur bei aktiver WLAN-Internetverbindung.</p>");
  page += F("<form id='news' method='post' action='/news/save'><label><input style='width:auto' type='checkbox' name='newson' value='1'");
  if (newsConfig_.enabled) page += F(" checked");
  page += F("> News-Ticker aktivieren</label>");
  page += F("<label>Aktualisierungsintervall (1–1440 Minuten)</label><input type='number' name='nmin' min='1' max='1440' step='1' required value='");
  page += String(newsConfig_.refreshMinutes);
  page += F("'>");
  for (uint8_t i = 0; i < NewsService::MAX_FEEDS; ++i) {
    page += F("<label>RSS-/Atom-Feed ");
    page += String(i + 1);
    page += F("</label><input type='url' name='nurl");
    page += String(i + 1);
    page += F("' maxlength='191' placeholder='https://example.org/feed.xml' value='");
    page += htmlEscape(String(newsConfig_.urls[i]));
    page += F("'>");
  }
  page += F("<button type='submit'>News-Einstellungen speichern</button></form></section>");
  page += F("<form id='wifi' method='post' action='/wifi/save'><button type='submit'>Speichern und verbinden</button></form>"
            "<section><h3>Verbindungsstatus</h3><p id='status'>Wird geladen ...</p>"
            "<small>Einrichtungs-WLAN: ATS-Mini-OS-Setup · Adresse: 10.1.1.1</small></section>"
            "<script>async function status(){try{let r=await fetch('/wifi/status');let j=await r.json();"
            "document.getElementById('status').textContent=j.connected?'Verbunden: '+j.ssid+' · '+j.ip:"
            "'Nicht mit Router verbunden · Setup-Seite bleibt erreichbar';}catch(e){}}status();setInterval(status,3000);</script>"
            "</main></body></html>");
  server_.send(200, "text/html; charset=utf-8", page);
}

void WifiClient::handleSave() {
  bool any = false;
  for (uint8_t i = 0; i < NETWORK_COUNT; ++i) {
    const String ssidArg = server_.arg(String("ssid") + String(i + 1));
    const String passArg = server_.arg(String("pass") + String(i + 1));
    if (server_.hasArg(String("ssid") + String(i + 1))) {
      ssidArg.toCharArray(ssid_[i], sizeof(ssid_[i]));
      any |= ssid_[i][0] != '\0';
    }
    // An empty password field deliberately preserves the previously saved secret.
    if (passArg.length()) passArg.toCharArray(password_[i], sizeof(password_[i]));
    if (!ssid_[i][0]) password_[i][0] = '\0';
  }
  if (!saveNetworks()) {
    server_.send(500, "text/plain; charset=utf-8", "Speichern fehlgeschlagen: NVS nicht verfügbar");
    return;
  }
  server_.send(200, "text/html; charset=utf-8",
    "<meta charset='utf-8'><meta http-equiv='refresh' content='5;url=/'><p>WLAN-Daten gespeichert. "
    "Verbindungsaufbau läuft. Diese Seite lädt in fünf Sekunden neu.</p>");
  WiFi.disconnect(false, false);
  if (any && stationEnabled()) connectNextNetwork();
}


void WifiClient::handleNewsSave() {
  if (!preferencesReady_) {
    server_.send(500, "text/plain; charset=utf-8", "Speichern fehlgeschlagen: NVS nicht verfügbar");
    return;
  }
  const String intervalArg = server_.arg("nmin");
  if (!intervalArg.length()) {
    server_.send(400, "text/plain; charset=utf-8", "Bitte ein Aktualisierungsintervall von 1 bis 1440 Minuten eingeben.");
    return;
  }
  char* end = nullptr;
  const long interval = strtol(intervalArg.c_str(), &end, 10);
  if (!end || *end != '\0' || interval < 1 || interval > 1440) {
    server_.send(400, "text/plain; charset=utf-8", "Ungültiges Intervall. Erlaubt sind 1 bis 1440 Minuten.");
    return;
  }
  NewsService::Configuration next;
  next.enabled = server_.hasArg("newson");
  next.refreshMinutes = static_cast<uint16_t>(interval);
  for (uint8_t i = 0; i < NewsService::MAX_FEEDS; ++i) {
    const String argName = String("nurl") + String(i + 1);
    String value = server_.arg(argName);
    value.trim();
    if (value.length() >= NewsService::MAX_URL_BYTES) {
      server_.send(400, "text/plain; charset=utf-8", "Eine Feed-Adresse ist zu lang (maximal 191 Zeichen).");
      return;
    }
    value.toCharArray(next.urls[i], sizeof(next.urls[i]));
    if (value.length() && !NewsService::validFeedUrl(next.urls[i])) {
      server_.send(400, "text/plain; charset=utf-8", "Feed-Adressen müssen mit https:// beginnen und dürfen keine Leerzeichen enthalten.");
      return;
    }
  }
  bool ok = preferences_.putBool("newson", next.enabled) == sizeof(bool);
  ok &= preferences_.putUShort("nmin", next.refreshMinutes) == sizeof(uint16_t);
  for (uint8_t i = 0; i < NewsService::MAX_FEEDS; ++i) {
    char key[8];
    snprintf(key, sizeof(key), "nurl%u", static_cast<unsigned>(i + 1));
    ok &= preferences_.putString(key, next.urls[i]) > 0 || next.urls[i][0] == '\0';
  }
  if (!ok) {
    server_.send(500, "text/plain; charset=utf-8", "News-Einstellungen konnten nicht vollständig gespeichert werden.");
    return;
  }
  newsConfig_ = next;
  if (newsService_) newsService_->setConfiguration(newsConfig_);
  server_.send(200, "text/html; charset=utf-8",
    "<meta charset='utf-8'><meta http-equiv='refresh' content='2;url=/'><p>News-Einstellungen gespeichert.</p>");
}

void WifiClient::handleStatus() {
  const bool isConnected = connected();
  String json = "{\"connected\":";
  json += isConnected ? "true" : "false";
  json += ",\"ssid\":\"";
  if (isConnected) json += WiFi.SSID();
  json += "\",\"ip\":\"";
  json += isConnected ? WiFi.localIP().toString() : String("10.1.1.1");
  json += "\"}";
  server_.send(200, "application/json", json);
}

void WifiClient::connectNextNetwork() {
  if (!stationEnabled()) return;
  for (uint8_t n = 0; n < NETWORK_COUNT; ++n) {
    activeNetwork_ = static_cast<uint8_t>((activeNetwork_ + n) % NETWORK_COUNT);
    if (!ssid_[activeNetwork_][0]) continue;
    WiFi.begin(ssid_[activeNetwork_], password_[activeNetwork_]);
    lastAttemptMs_ = millis();
    Serial.printf("[WIFI] trying saved network slot %u\n", static_cast<unsigned>(activeNetwork_ + 1));
    return;
  }
  Serial.println("[WIFI] no configured SSID found");
}

void WifiClient::update() {
  if (serverStarted_) server_.handleClient();
  const wl_status_t status = WiFi.status();
  const uint32_t now = millis();
  if (status != lastStatus_) {
    lastStatus_ = status;
    if (status == WL_CONNECTED) {
      Serial.printf("[WIFI] connected; IP=%s RSSI=%d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED) {
      Serial.printf("[WIFI] network unavailable/connection failed (status=%d)\n", static_cast<int>(status));
    } else if (status == WL_DISCONNECTED) {
      Serial.println("[WIFI] disconnected; setup AP remains active");
    }
  }
  if (stationEnabled() && status != WL_CONNECTED && now - lastAttemptMs_ >= RETRY_INTERVAL_MS) {
    lastAttemptMs_ = now;
    connectNextNetwork();
  }
  if (status != WL_CONNECTED && now - lastLogMs_ >= 15000) {
    lastLogMs_ = now;
    Serial.printf("[WIFI] waiting for router; setup page http://10.1.1.1/ (status=%d)\n",
                  static_cast<int>(status));
  }
}

bool WifiClient::connected() const {
  return stationEnabled() && WiFi.status() == WL_CONNECTED;
}
