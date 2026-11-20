#include "WifiClient.h"
#include "../news/NewsService.h"
#include "../system/BootDiagnostics.h"
#include <ESPmDNS.h>
#include "TcpRemoteService.h"

namespace {
constexpr char SETTINGS_NAMESPACE[] = "network";
constexpr char SETUP_AP_SSID[] = "MINI-OS-WEB";
constexpr int SETUP_AP_CHANNEL = 10;
constexpr int SETUP_AP_MAX_CLIENTS = 3;
const IPAddress SETUP_IP(10, 1, 1, 1);
const IPAddress SETUP_GATEWAY(10, 1, 1, 1);
const IPAddress SETUP_SUBNET(255, 255, 255, 0);

void applyWifiTransmitPower(WiFiEvent_t event) {
  (void)event;
  WiFi.setTxPower(WIFI_POWER_17dBm);
}

void registerWifiPowerEvents() {
  static bool registered = false;
  if (registered) return;
  WiFi.onEvent(applyWifiTransmitPower, ARDUINO_EVENT_WIFI_AP_START);
  WiFi.onEvent(applyWifiTransmitPower, ARDUINO_EVENT_WIFI_STA_START);
  registered = true;
}
}

void WifiClient::setNewsService(NewsService* service) {
  newsService_ = service;
  if (newsService_) newsService_->setConfiguration(newsConfig_);
}

void WifiClient::setWeatherService(WeatherService* service) {
  weatherService_ = service;
  BootDiagnostics::printf("[WEATHER] service attached=%s", weatherService_ ? "yes" : "no");
}

void WifiClient::setTcpRemoteService(TcpRemoteService* service) {
  tcpRemoteService_ = service;
}

bool WifiClient::stationEnabled() const {
  return mode_ == Mode::AP_CONNECT || mode_ == Mode::CONNECT || mode_ == Mode::SYNC_ONLY;
}

bool WifiClient::accessPointEnabled() const {
  return mode_ == Mode::AP_ONLY || mode_ == Mode::AP_CONNECT;
}

void WifiClient::begin() {
  BootDiagnostics::printf("[WIFI][BOOT] begin enter free_heap=%u max_block=%u", static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  registerWifiPowerEvents();
  preferencesReady_ = preferences_.begin(SETTINGS_NAMESPACE, false, "settings");
  if (preferencesReady_) {
    loadNetworks();
    wifiScanHidden_ = preferences_.getBool("wifiscanhidden", false);
    tcpRemoteEnabled_ = preferences_.getBool("tcpon", false);
    loadNewsConfig();
    const uint8_t savedMode = preferences_.getUChar("wmode", static_cast<uint8_t>(Mode::AP_CONNECT));
    mode_ = savedMode <= static_cast<uint8_t>(Mode::SYNC_ONLY)
      ? static_cast<Mode>(savedMode) : Mode::AP_CONNECT;
  }
  if (newsService_) newsService_->setConfiguration(newsConfig_);
  // Keep the radio offline at boot. The saved operating mode is applied
  // only when a layout or an online feature explicitly requests networking.
  started_ = false;
  apActive_ = false;
  BootDiagnostics::printf("[WIFI][BOOT] deferred; configured_mode=%u free_heap=%u",
    static_cast<unsigned>(mode_), static_cast<unsigned>(ESP.getFreeHeap()));
}

void WifiClient::suspend() {
  if (!started_) return;
  scanner_.stop();
  connectCycleActive_ = false;
  attemptInProgress_ = false;
  if (tcpRemoteService_) tcpRemoteService_->stop();
  stopMdns();
  BootDiagnostics::write("[WIFI] temporarily suspended; stopping web server and radio");
  if (serverStarted_) {
    server_.stop();
    serverStarted_ = false;
  }
  WiFi.softAPdisconnect(false);
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  apActive_ = false;
  started_ = false;
  syncOnlyStartedMs_ = 0;
  syncOnlyConnectedAtMs_ = 0;
  lastStatus_ = WL_IDLE_STATUS;
}

bool WifiClient::ensureStarted() {
  if (started_) return true;
  // OFF is a runtime state, not a permanent veto: a feature that genuinely
  // requires networking may activate the saved/default network mode.
  if (mode_ == Mode::OFF) mode_ = Mode::AP_CONNECT;
  syncOnlyStartedMs_ = mode_ == Mode::SYNC_ONLY ? (millis() ? millis() : 1) : 0;
  syncOnlyConnectedAtMs_ = 0;
  BootDiagnostics::write("[WIFI] on-demand activation requested");
  applyMode();
  started_ = true;
  if (mode_ != Mode::SYNC_ONLY) startWebServer();
  return true;
}

void WifiClient::startDataWorkers() {
  if (newsService_ && !newsWorkerStarted_) {
    const NewsService::Configuration config = newsService_->configuration();
    bool hasFeed = false;
    for (uint8_t i = 0; i < NewsService::MAX_FEEDS; ++i)
      hasFeed = hasFeed || config.urls[i][0] != '\0';
    if (config.enabled && hasFeed) {
      newsWorkerStarted_ = newsService_->begin();
      BootDiagnostics::printf("[NEWS] lazy worker start=%s", newsWorkerStarted_ ? "success" : "failed");
    }
  }
  if (weatherService_ && !weatherWorkerStarted_ && !weatherConfigChecked_) {
    weatherWorkerStarted_ = weatherService_->begin();
    weatherConfigChecked_ = !weatherService_->hasConfiguredLocation();
    BootDiagnostics::printf("[WEATHER] lazy worker start=%s",
      weatherWorkerStarted_ ? "success" :
      weatherConfigChecked_ ? "skipped: no postal code" : "failed; retry scheduled");
  }
}

void WifiClient::stopMdns() {
  if (mdnsStarted_) {
    MDNS.end();
    mdnsStarted_ = false;
  }
  mdnsRetryMs_ = 0;
}

void WifiClient::updateMdns() {
  const bool networkAvailable = started_ && mode_ != Mode::OFF &&
    mode_ != Mode::SYNC_ONLY && (apActive_ || WiFi.status() == WL_CONNECTED);
  if (!networkAvailable) {
    stopMdns();
    return;
  }
  if (mdnsStarted_) return;
  const uint32_t now = millis();
  if (mdnsRetryMs_ && now - mdnsRetryMs_ < 5000) return;
  mdnsRetryMs_ = now ? now : 1;
  if (MDNS.begin("mini-os")) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted_ = true;
    BootDiagnostics::write("[WIFI][MDNS] mini-os.local HTTP service started");
  } else {
    BootDiagnostics::write("[WIFI][MDNS] start failed; retry scheduled");
  }
}

void WifiClient::applyMode() {
  scanner_.stop();
  if (tcpRemoteService_) tcpRemoteService_->stop();
  stopMdns();
  connectCycleActive_ = false;
  attemptInProgress_ = false;
  syncOnlyConnectedAtMs_ = 0;
  WiFi.setAutoReconnect(false);
  WiFi.softAPdisconnect(false);
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
    // Keep Mini_OS's setup SSID while matching the reference AP network settings.
    apActive_ = WiFi.softAP(SETUP_AP_SSID, nullptr,
                            SETUP_AP_CHANNEL, false, SETUP_AP_MAX_CLIENTS);
    if (!apActive_) {
      BootDiagnostics::write("[WIFI][AP] softAP start failed");
    } else if (!WiFi.softAPConfig(SETUP_IP, SETUP_GATEWAY, SETUP_SUBNET)) {
      BootDiagnostics::write("[WIFI][AP] static IP configuration failed");
    }
  }
  if (stationEnabled()) connectNextNetwork();
  Serial.printf("[WIFI] mode=%u AP=%s STA=%s\\n",
    static_cast<unsigned>(mode_), apActive_ ? "ON" : "OFF",
    stationEnabled() ? "ON" : "OFF");
}

bool WifiClient::setMode(Mode mode) {
  if (static_cast<uint8_t>(mode) > static_cast<uint8_t>(Mode::SYNC_ONLY)) return false;
  if (preferencesReady_ &&
      preferences_.putUChar("wmode", static_cast<uint8_t>(mode)) != sizeof(uint8_t)) return false;
  mode_ = mode;
  syncOnlyStartedMs_ = mode_ == Mode::SYNC_ONLY ? (millis() ? millis() : 1) : 0;
  syncOnlyConnectedAtMs_ = 0;
  if (mode_ == Mode::OFF) {
    if (serverStarted_) {
      server_.stop();
      serverStarted_ = false;
    }
    applyMode();
    started_ = false;
    return true;
  }
  if (!started_) return ensureStarted();
  applyMode();
  if (mode_ == Mode::SYNC_ONLY) {
    if (serverStarted_) {
      server_.stop();
      serverStarted_ = false;
    }
  } else {
    startWebServer();
  }
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
  if (serverStarted_) return;
  BootDiagnostics::printf("[WIFI][WEB] preparing routes free_heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));
  if (!routesRegistered_) {
    server_.on("/", HTTP_GET, [this]() { handleRoot(); });
    server_.on("/wifi/save", HTTP_POST, [this]() { handleSave(); });
    server_.on("/wifi/status", HTTP_GET, [this]() { handleStatus(); });
    server_.on("/news/save", HTTP_POST, [this]() { handleNewsSave(); });
    server_.on("/weather/save", HTTP_POST, [this]() { handleWeatherSave(); });
    server_.onNotFound([this]() { server_.send(404, "text/plain", "Not found"); });
    routesRegistered_ = true;
  }
  BootDiagnostics::write("[WIFI][WEB] calling WebServer.begin");
  server_.begin();
  serverStarted_ = true;
  BootDiagnostics::write("[WIFI][WEB] WebServer.begin returned");
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
            "<p>Bis zu drei Netzwerke speichern. Verbunden wird mit dem stärksten gefundenen gespeicherten Netzwerk.</p>");
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
  page += F("<section><h2>Wetterbericht</h2><p>Standort über deutsche Postleitzahl einstellen. Wetterdaten werden automatisch alle 15 Minuten aktualisiert.</p>");
  page += F("<form method='post' action='/weather/save'><label>Postleitzahl</label><input name='postal' inputmode='numeric' pattern='[0-9]{5}' minlength='5' maxlength='5' required value='");
  page += weatherService_ ? htmlEscape(weatherService_->postalCode()) : String("");
  page += F("'><button type='submit'>Wetter-Standort speichern</button></form></section>");
  page += F("<section><label><input style='width:auto' type='checkbox' name='wifiscanhidden' value='1' form='wifi'");
  if (wifiScanHidden_) page += F(" checked");
  page += F("> Versteckte WLAN-Netzwerke bei der Suche berücksichtigen</label></section>");
  page += F("<section><label><input style='width:auto' type='checkbox' name='tcpon' value='1' form='wifi'");
  if (tcpRemoteEnabled_) page += F(" checked");
  page += F("> TCP-Fernsteuerung aktivieren (Port 60000)</label></section>");
  page += F("<form id='wifi' method='post' action='/wifi/save'><button type='submit'>Speichern und verbinden</button></form>"
            "<section><h3>Verbindungsstatus</h3><p id='status'>Wird geladen ...</p>"
            "<small>Einrichtungs-WLAN: MINI-OS-WEB · Adresse: 10.1.1.1</small></section>"
            "<script>async function status(){try{let r=await fetch('/wifi/status');let j=await r.json();"
            "let s=j.connected?'Router: '+j.ssid+' · '+j.sta_ip:(j.ap_active?'Setup-WLAN: '+j.ssid+' · '+j.ap_ip:'Nicht mit Router verbunden');"
            "if(j.connected&&j.ap_active)s+=' · Setup: '+j.ap_ip;"
            "document.getElementById('status').textContent=s;}catch(e){}}status();setInterval(status,3000);</script>"
            "</main></body></html>");
  server_.send(200, "text/html; charset=utf-8", page);
}

void WifiClient::handleSave() {
  for (uint8_t i = 0; i < NETWORK_COUNT; ++i) {
    const String ssidArg = server_.arg(String("ssid") + String(i + 1));
    const String passArg = server_.arg(String("pass") + String(i + 1));
    if (server_.hasArg(String("ssid") + String(i + 1))) {
      ssidArg.toCharArray(ssid_[i], sizeof(ssid_[i]));
    }
    // An empty password field deliberately preserves the previously saved secret.
    if (passArg.length()) passArg.toCharArray(password_[i], sizeof(password_[i]));
    if (!ssid_[i][0]) password_[i][0] = '\0';
  }
  if (!saveNetworks()) {
    server_.send(500, "text/plain; charset=utf-8", "Speichern fehlgeschlagen: NVS nicht verfügbar");
    return;
  }
  wifiScanHidden_ = server_.hasArg("wifiscanhidden");
  if (preferences_.putBool("wifiscanhidden", wifiScanHidden_) != sizeof(bool)) {
    server_.send(500, "text/plain; charset=utf-8", "Die WLAN-Suchoption konnte nicht gespeichert werden.");
    return;
  }
  tcpRemoteEnabled_ = server_.hasArg("tcpon");
  if (preferences_.putBool("tcpon", tcpRemoteEnabled_) != sizeof(bool)) {
    server_.send(500, "text/plain; charset=utf-8", "Die TCP-Fernsteuerung konnte nicht gespeichert werden.");
    return;
  }
  server_.send(200, "text/html; charset=utf-8",
    "<meta charset='utf-8'><meta http-equiv='refresh' content='5;url=/'><p>WLAN-Daten gespeichert. "
    "Verbindungsaufbau läuft. Diese Seite lädt in fünf Sekunden neu.</p>");
  WiFi.disconnect(false, false);
  if (stationEnabled()) connectNextNetwork();
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

void WifiClient::handleWeatherSave() {
  BootDiagnostics::write("[WEATHER] POST /weather/save received");
  if (!weatherService_) {
    BootDiagnostics::write("[WEATHER] ERROR: service pointer is null");
    server_.send(503, "text/plain; charset=utf-8", "Wetterdienst nicht verfügbar.");
    return;
  }
  const String postal = server_.arg("postal");
  BootDiagnostics::printf("[WEATHER] POST postal='%s' length=%u",
    postal.c_str(), static_cast<unsigned>(postal.length()));
  const bool saved = weatherService_->setPostalCode(postal.c_str());
  BootDiagnostics::printf("[WEATHER] POST save result=%s", saved ? "success" : "FAILED");
  if (!saved) {
    server_.send(400, "text/plain; charset=utf-8", "Bitte eine gültige fünfstellige deutsche Postleitzahl eingeben oder Speichern prüfen.");
    return;
  }
  weatherConfigChecked_ = false;
  if (connected() && dataServicesAllowed()) startDataWorkers();
  server_.send(200, "text/html; charset=utf-8",
    "<meta charset='utf-8'><meta http-equiv='refresh' content='2;url=/'><p>Wetter-Standort gespeichert. Die Wetterdaten werden neu geladen.</p>");
}

void WifiClient::handleStatus() {
  const bool isConnected = connected();
  const String ssid = networkName();
  const String ip = ipAddress();
  String escapedSsid;
  escapedSsid.reserve(ssid.length() + 8);
  for (size_t i = 0; i < ssid.length(); ++i) {
    const char c = ssid[i];
    if (c == '"' || c == '\\') { escapedSsid += '\\'; escapedSsid += c; }
    else if (c == '\n') escapedSsid += F("\\n");
    else if (c == '\r') escapedSsid += F("\\r");
    else if (static_cast<uint8_t>(c) >= 0x20) escapedSsid += c;
  }
  String json;
  json.reserve(192);
  json = "{\"connected\":";
  json += isConnected ? "true" : "false";
  json += ",\"ssid\":\"" + escapedSsid + "\",\"ip\":\"" + ip + "\"";
  json += ",\"mode\":" + String(static_cast<uint8_t>(mode_));
  json += ",\"rssi\":" + String(isConnected ? WiFi.RSSI() : 0);
  json += ",\"sta_ip\":\"" + (isConnected ? WiFi.localIP().toString() : String("")) + "\"";
  json += ",\"ap_ip\":\"" + (apActive_ ? WiFi.softAPIP().toString() : String("")) + "\"";
  json += ",\"ap_active\":" + String(apActive_ ? "true" : "false");
  json += ",\"ap_clients\":" + String(apActive_ ? WiFi.softAPgetStationNum() : 0);
  json += ",\"hidden_scan\":" + String(wifiScanHidden_ ? "true" : "false");
  json += ",\"mdns\":" + String(mdnsStarted_ ? "true" : "false") + "}";
  server_.send(200, "application/json", json);
}

void WifiClient::connectNextNetwork() {
  if (!stationEnabled()) return;
  scanner_.stop();
  connectCycleActive_ = false;
  attemptInProgress_ = false;
  hiddenProbeIndex_ = 0;
  hiddenProbeCompleted_ = false;
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);

  bool hasSavedNetwork = false;
  for (uint8_t i = 0; i < NETWORK_COUNT; ++i)
    hasSavedNetwork = hasSavedNetwork || ssid_[i][0] != '\0';
  if (!hasSavedNetwork) {
    lastAttemptMs_ = millis();
    BootDiagnostics::write("[WIFI] no saved router network; station remains idle");
    return;
  }

  connectCycleStartedMs_ = millis();
  connectCycleActive_ = true;
  startNetworkScan();
}

void WifiClient::startNetworkScan() {
  const char* saved[NETWORK_COUNT] = { ssid_[0], ssid_[1], ssid_[2] };
  if (!scanner_.begin(saved, NETWORK_COUNT, wifiScanHidden_)) {
    lastAttemptMs_ = millis();
    BootDiagnostics::write("[WIFI] asynchronous scan could not start");
    if (wifiScanHidden_) tryNextScannedNetwork();
    return;
  }
  attemptInProgress_ = false;
  lastAttemptMs_ = millis();
  BootDiagnostics::write("[WIFI] asynchronous network scan started");
}

void WifiClient::tryNextScannedNetwork() {
  WifiScanner::Candidate candidate;
  if (scanner_.next(candidate)) {
    activeNetwork_ = candidate.networkIndex;
    const char* password = candidate.open || !password_[activeNetwork_][0]
      ? nullptr : password_[activeNetwork_];
    WiFi.begin(ssid_[activeNetwork_], password, candidate.channel, candidate.bssid);
    lastAttemptMs_ = millis();
    attemptInProgress_ = true;
    Serial.printf("[WIFI] trying strongest candidate slot %u RSSI=%d dBm channel=%ld\\n",
      static_cast<unsigned>(activeNetwork_ + 1),
      static_cast<int>(candidate.rssi),
      static_cast<long>(candidate.channel));
    return;
  }

  // Hidden APs do not advertise their SSID in scan results. When the user
  // enables hidden-network search, probe each saved profile once per
  // connection cycle without a BSSID/channel restriction.
  if (wifiScanHidden_ && !hiddenProbeCompleted_) {
    while (hiddenProbeIndex_ < NETWORK_COUNT) {
      const uint8_t index = hiddenProbeIndex_++;
      if (!ssid_[index][0]) continue;
      activeNetwork_ = index;
      const char* password = password_[index][0] ? password_[index] : nullptr;
      WiFi.begin(ssid_[index], password);
      lastAttemptMs_ = millis();
      attemptInProgress_ = true;
      Serial.printf("[WIFI] probing hidden saved network slot %u\\n",
        static_cast<unsigned>(activeNetwork_ + 1));
      return;
    }
    hiddenProbeCompleted_ = true;
  }

  attemptInProgress_ = false;
  lastAttemptMs_ = millis();
}

void WifiClient::update(bool timeSynchronized) {
  if (!started_) return;
  if (serverStarted_ && mode_ != Mode::SYNC_ONLY) server_.handleClient();

  const uint32_t now = millis();
  const wl_status_t status = WiFi.status();
  if (status != lastStatus_) {
    const wl_status_t previousStatus = lastStatus_;
    lastStatus_ = status;
    if (status == WL_CONNECTED) {
      connectCycleActive_ = false;
      attemptInProgress_ = false;
      scanner_.stop();
      WiFi.setAutoReconnect(true);
      if (mode_ == Mode::SYNC_ONLY && !syncOnlyConnectedAtMs_)
        syncOnlyConnectedAtMs_ = now ? now : 1;
      BootDiagnostics::printf("[WIFI] connected ssid='%s' ip=%s rssi=%d",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
      Serial.printf("[WIFI] connected; IP=%s RSSI=%d dBm\\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else {
      BootDiagnostics::printf("[WIFI] connection state changed status=%d", static_cast<int>(status));
      if (previousStatus == WL_CONNECTED && stationEnabled() && mode_ != Mode::SYNC_ONLY) {
        WiFi.setAutoReconnect(false);
        connectNextNetwork();
      }
      if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED)
        Serial.printf("[WIFI] association failed (status=%d)\\n", static_cast<int>(status));
      else if (status == WL_DISCONNECTED)
        Serial.println("[WIFI] disconnected; setup AP remains active");
    }
  }

  if (stationEnabled() && status != WL_CONNECTED && connectCycleActive_) {
    if (now - connectCycleStartedMs_ >= TOTAL_CONNECT_TIMEOUT_MS) {
      scanner_.stop();
      WiFi.disconnect(false, false);
      connectCycleActive_ = false;
      attemptInProgress_ = false;
      lastAttemptMs_ = now;
      BootDiagnostics::write("[WIFI] total connection timeout; setup AP remains available where enabled");
    } else if (scanner_.scanning()) {
      if (scanner_.update()) tryNextScannedNetwork();
    } else if (attemptInProgress_ && now - lastAttemptMs_ >= ATTEMPT_TIMEOUT_MS) {
      WiFi.disconnect(false, false);
      attemptInProgress_ = false;
      tryNextScannedNetwork();
    } else if (!attemptInProgress_ && now - lastAttemptMs_ >= RESCAN_DELAY_MS) {
      startNetworkScan();
    }
  }

  const bool hasSavedNetwork = ssid_[0][0] || ssid_[1][0] || ssid_[2][0];
  if (stationEnabled() && hasSavedNetwork && status != WL_CONNECTED &&
      !connectCycleActive_ && mode_ != Mode::SYNC_ONLY &&
      now - lastAttemptMs_ >= RECONNECT_DELAY_MS) {
    connectNextNetwork();
  }

  bool newsWorkerNeeded = false;
  if (newsService_ && !newsWorkerStarted_) {
    const NewsService::Configuration config = newsService_->configuration();
    bool hasFeed = false;
    for (uint8_t i = 0; i < NewsService::MAX_FEEDS; ++i)
      hasFeed = hasFeed || config.urls[i][0] != '\0';
    newsWorkerNeeded = config.enabled && hasFeed;
  }
  const bool weatherWorkerNeeded = weatherService_ && !weatherWorkerStarted_ && !weatherConfigChecked_;
  if (status == WL_CONNECTED && dataServicesAllowed() &&
      (newsWorkerNeeded || weatherWorkerNeeded) &&
      (!lastWorkerStartAttemptMs_ || now - lastWorkerStartAttemptMs_ >= 5000)) {
    lastWorkerStartAttemptMs_ = now ? now : 1;
    startDataWorkers();
  }

  updateMdns();
  if (tcpRemoteService_) {
    const bool networkAvailable = apActive_ || status == WL_CONNECTED;
    tcpRemoteService_->update(networkAvailable, tcpRemoteEnabled_ && mode_ != Mode::SYNC_ONLY);
  }

  if (mode_ == Mode::SYNC_ONLY) {
    bool finishSync = false;
    if (status == WL_CONNECTED) {
      if (!syncOnlyConnectedAtMs_) syncOnlyConnectedAtMs_ = now ? now : 1;
      if (timeSynchronized) {
        BootDiagnostics::write("[WIFI][SYNC_ONLY] system clock synchronized; shutting down WLAN");
        finishSync = true;
      } else if (now - syncOnlyConnectedAtMs_ >= SYNC_TIME_TIMEOUT_MS) {
        BootDiagnostics::write("[WIFI][SYNC_ONLY] NTP synchronization timeout; shutting down WLAN");
        finishSync = true;
      }
    } else if (syncOnlyStartedMs_ && now - syncOnlyStartedMs_ >= TOTAL_CONNECT_TIMEOUT_MS) {
      BootDiagnostics::write("[WIFI][SYNC_ONLY] connection timeout; shutting down WLAN");
      finishSync = true;
    }
    if (finishSync) {
      suspend();
      return;
    }
  }

  if (status != WL_CONNECTED && now - lastLogMs_ >= 15000) {
    lastLogMs_ = now;
    Serial.printf("[WIFI] waiting for router; setup page http://10.1.1.1/ when AP mode is active (status=%d)\\n",
                  static_cast<int>(status));
  }
}

bool WifiClient::connected() const {
  return started_ && stationEnabled() && WiFi.status() == WL_CONNECTED;
}
