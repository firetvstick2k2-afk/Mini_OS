#include "WifiClient.h"
#include <string.h>
#include "../display/SplashService.h"
#include <LittleFS.h>
#include "../news/NewsService.h"
#include "../system/BootDiagnostics.h"

namespace {
constexpr char SETTINGS_NAMESPACE[] = "network";
constexpr char SETUP_AP_SSID[] = "ATS-Mini-OS-Setup";
const IPAddress SETUP_IP(10, 1, 1, 1);
const IPAddress SETUP_GATEWAY(10, 1, 1, 1);
const IPAddress SETUP_SUBNET(255, 255, 255, 0);
const char* wifiStatusName(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "OTHER";
  }
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

bool WifiClient::stationEnabled() const {
  return mode_ == Mode::AP_CONNECT || mode_ == Mode::CONNECT || mode_ == Mode::SYNC_ONLY;
}

bool WifiClient::accessPointEnabled() const {
  return mode_ == Mode::AP_ONLY || mode_ == Mode::AP_CONNECT;
}

void WifiClient::begin() {
  BootDiagnostics::printf("[WIFI][BOOT] begin enter free_heap=%u max_block=%u", static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  preferencesReady_ = preferences_.begin(SETTINGS_NAMESPACE, false, "settings");
  if (preferencesReady_) {
    loadNetworks();
    loadNewsConfig();
    const uint8_t savedMode = preferences_.getUChar("wmode", static_cast<uint8_t>(Mode::AP_CONNECT));
    mode_ = savedMode <= static_cast<uint8_t>(Mode::SYNC_ONLY)
      ? static_cast<Mode>(savedMode) : Mode::AP_CONNECT;
    BootDiagnostics::printf("[WIFI][NVS] preferences ready mode=%u", static_cast<unsigned>(mode_));
    for (uint8_t i = 0; i < NETWORK_COUNT; ++i)
      BootDiagnostics::printf("[WIFI][NVS] slot=%u configured=%u ssid_len=%u password_present=%u",
        static_cast<unsigned>(i + 1), ssid_[i][0] ? 1U : 0U,
        static_cast<unsigned>(strlen(ssid_[i])), password_[i][0] ? 1U : 0U);
  } else {
    BootDiagnostics::write("[WIFI][NVS] preferences unavailable; network slots and mode use defaults");
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
  BootDiagnostics::write("[WIFI] temporarily suspended for digital decoder");
  if (serverStarted_) {
    server_.stop();
    serverStarted_ = false;
  }
  WiFi.softAPdisconnect(false);
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_OFF);
  apActive_ = false;
  started_ = false;
}

bool WifiClient::ensureStarted() {
  if (started_) {
    BootDiagnostics::printf("[WIFI][START] already active mode=%u status=%s slot=%u",
      static_cast<unsigned>(mode_), wifiStatusName(WiFi.status()), static_cast<unsigned>(activeNetwork_ + 1));
    return true;
  }
  // OFF is a runtime state, not a permanent veto: a feature that genuinely
  // requires networking may activate the saved/default network mode.
  if (mode_ == Mode::OFF) mode_ = Mode::AP_CONNECT;
  BootDiagnostics::printf("[WIFI][START] on-demand activation mode=%u heap=%u",
    static_cast<unsigned>(mode_), static_cast<unsigned>(ESP.getFreeHeap()));
  applyMode();
  started_ = true;
  startWebServer();
  if (newsService_) {
    const bool ok = newsService_->begin();
    BootDiagnostics::printf("[NEWS] lazy worker start=%s", ok ? "success" : "failed");
  }
  if (weatherService_) {
    const bool ok = weatherService_->begin();
    BootDiagnostics::printf("[WEATHER] lazy worker start=%s", ok ? "success" : "failed");
  }
  return true;
}

void WifiClient::applyMode() {
  BootDiagnostics::printf("[WIFI][MODE] apply mode=%u started=%u status_before=%s",
    static_cast<unsigned>(mode_), started_ ? 1U : 0U, wifiStatusName(WiFi.status()));
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
    WiFi.softAPConfig(SETUP_IP, SETUP_GATEWAY, SETUP_SUBNET);
    apActive_ = WiFi.softAP(SETUP_AP_SSID);
    BootDiagnostics::printf("[WIFI][AP] start result=%s ip=%s", apActive_ ? "OK" : "FAILED",
      WiFi.softAPIP().toString().c_str());
  }
  if (stationEnabled()) {
    bool found = false;
    for (uint8_t i = 0; i < NETWORK_COUNT; ++i) {
      if (ssid_[i][0]) { activeNetwork_ = i; found = true; break; }
    }
    if (found) {
      const wl_status_t beginStatus = WiFi.begin(ssid_[activeNetwork_], password_[activeNetwork_]);
      lastAttemptMs_ = millis();
      BootDiagnostics::printf("[WIFI][ATTEMPT] source=mode-start slot=%u begin_status=%s(%d) timeout_ms=%lu",
        static_cast<unsigned>(activeNetwork_ + 1), wifiStatusName(beginStatus), static_cast<int>(beginStatus),
        static_cast<unsigned long>(RETRY_INTERVAL_MS));
      Serial.printf("[WIFI] connecting to saved network slot %u\\n",
                    static_cast<unsigned>(activeNetwork_ + 1));
    } else {
      BootDiagnostics::write("[WIFI][ATTEMPT] no configured router SSID in slots 1-3");
      Serial.println("[WIFI] no saved router network");
    }
  }
  BootDiagnostics::printf("[WIFI][MODE] applied mode=%u AP=%s STA=%s status=%s slot=%u",
    static_cast<unsigned>(mode_), apActive_ ? "ON" : "OFF", stationEnabled() ? "ON" : "OFF",
    wifiStatusName(WiFi.status()), static_cast<unsigned>(activeNetwork_ + 1));
  Serial.printf("[WIFI] mode=%u AP=%s STA=%s\\n",
    static_cast<unsigned>(mode_), apActive_ ? "ON" : "OFF",
    stationEnabled() ? "ON" : "OFF");
}

bool WifiClient::setMode(Mode mode) {
  BootDiagnostics::printf("[WIFI][MODE] request old=%u new=%u started=%u",
    static_cast<unsigned>(mode_), static_cast<unsigned>(mode), started_ ? 1U : 0U);
  if (static_cast<uint8_t>(mode) > static_cast<uint8_t>(Mode::SYNC_ONLY)) {
    BootDiagnostics::write("[WIFI][MODE] rejected invalid mode value");
    return false;
  }
  if (preferencesReady_ &&
      preferences_.putUChar("wmode", static_cast<uint8_t>(mode)) != sizeof(uint8_t)) return false;
  mode_ = mode;
  if (mode_ == Mode::OFF) {
    if (serverStarted_) {
      server_.stop();
      serverStarted_ = false;
    }
    applyMode();
    started_ = false;
    return true;
  }
  if (started_) applyMode();
  else return ensureStarted();
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
  BootDiagnostics::printf("[WIFI][WEB] registering routes free_heap=%u", static_cast<unsigned>(ESP.getFreeHeap()));

  server_.on("/", HTTP_GET, [this]() { handleRoot(); });

  server_.on("/wifi/save", HTTP_POST, [this]() { handleSave(); });
  server_.on("/wifi/scan", HTTP_GET, [this]() { handleWifiScan(); });
  server_.on("/wifi/store", HTTP_POST, [this]() { handleWifiStore(); });
  server_.on("/wifi/clear", HTTP_POST, [this]() { handleWifiClear(); });
  server_.on("/splash.png", HTTP_GET, [this]() {
    if (!SplashService::exists()) return server_.send(404, "text/plain; charset=utf-8", "Kein Bootbild gespeichert.");
    File image = LittleFS.open(SplashService::IMAGE_PATH, "r");
    if (!image) return server_.send(500, "text/plain; charset=utf-8", "Bootbild konnte nicht geöffnet werden.");
    server_.streamFile(image, "image/png");
    image.close();
  });
  server_.on("/splash/upload", HTTP_POST,
    [this]() { handleSplashUploadComplete(); },
    [this]() { handleSplashUpload(); });

  server_.on("/wifi/status", HTTP_GET, [this]() { handleStatus(); });

  server_.on("/news/save", HTTP_POST, [this]() { handleNewsSave(); });

  server_.on("/weather/save", HTTP_POST, [this]() { handleWeatherSave(); });

  server_.onNotFound([this]() { server_.send(404, "text/plain", "Not found"); });

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
  page.reserve(7600);
  page += F("<!doctype html><html lang='de'><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<meta charset='utf-8'><title>ATS Mini OS - WLAN</title><style>"
            "body{font:16px Arial,sans-serif;background:#080d18;color:#eef4ff;margin:0;padding:18px}"
            "main{max-width:560px;margin:auto}h1{color:#65e6c4}section{background:#101d32;border:1px solid #176b83;"
            "padding:14px;margin:14px 0;border-radius:8px}label{display:block;margin:10px 0 4px;color:#a8c9d8}"
            "input,select{box-sizing:border-box;width:100%;padding:11px;border:1px solid #426276;border-radius:4px;background:#07111d;color:white}"
            "button{padding:12px 18px;background:#087e91;color:white;border:0;border-radius:4px;font-weight:bold;margin:4px}"
            "small{color:#9bb2c2}.net{display:flex;justify-content:space-between;align-items:center;gap:8px;padding:10px 0;border-bottom:1px solid #29445a}"
            ".net button{padding:8px 10px}.muted{color:#9bb2c2}</style></head><body><main>"
            "<h1>ATS Mini OS</h1><h2>WLAN-Einrichtung</h2>"
            "<section><h3>Gespeicherte Netzwerke</h3><div id='slots'>Wird geladen ...</div></section>"
            "<section><h3>Netzwerk hinzufügen</h3><p>Netzwerke in Reichweite suchen und auswählen.</p>"
            "<button type='button' id='scan'>WLAN-Netze suchen</button><div id='scanlist' class='muted'></div>"
            "<form id='wifiForm'><label>Ausgewählte SSID</label><input id='ssid' name='ssid' maxlength='32' required>"
            "<label>WLAN-Passwort</label><input id='password' name='password' type='password' maxlength='63' autocomplete='new-password'>"
            "<label>Speicherplatz</label><select id='slot' name='slot'></select>"
            "<button type='submit'>Speichern und verbinden</button></form><p id='message' class='muted'></p></section>");
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
  page += F("<section><h2>Bootbild</h2><p>PNG für die Startanzeige (320 × 170 Pixel, maximal 512 KB).</p>");
  if (SplashService::exists()) {
    page += F("<img src='/splash.png' alt='Aktuelles Bootbild' style='display:block;max-width:100%;height:auto;margin:10px 0;border:1px solid #426276'>");
  } else {
    page += F("<p><small>Aktuell ist kein Bootbild gespeichert.</small></p>");
  }
  page += F("<form method='post' action='/splash/upload' enctype='multipart/form-data'><label>PNG-Datei auswählen</label><input type='file' name='splash' accept='image/png,.png' required><button type='submit'>Bootbild hochladen</button></form></section>");
  page += F("<section><h3>Verbindungsstatus</h3><p id='status'>Wird geladen ...</p><small>Einrichtungs-WLAN: ATS-Mini-OS-Setup · Adresse: 10.1.1.1</small></section>"
            "<script>const $=id=>document.getElementById(id);"
            "async function loadSlots(){try{let r=await fetch('/wifi/status');let j=await r.json();$('slots').textContent='';$('slot').textContent='';"
            "j.slots.forEach((s,i)=>{let o=document.createElement('option');o.value=i+1;o.textContent='Platz '+(i+1)+(s?' – '+s:' – Frei');$('slot').appendChild(o);"
            "let row=document.createElement('div');row.className='net';let label=document.createElement('span');label.textContent='Platz '+(i+1)+': '+(s||'Nicht belegt');row.appendChild(label);"
            "if(s){let b=document.createElement('button');b.type='button';b.textContent='Löschen';b.onclick=()=>clearSlot(i+1);row.appendChild(b);}$('slots').appendChild(row);});"
            "}catch(e){$('slots').textContent='Speicherplätze konnten nicht geladen werden.';}}"
            "async function clearSlot(slot){if(!confirm('WLAN-Platz '+slot+' wirklich löschen?'))return;let r=await fetch('/wifi/clear',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'slot='+slot});$('message').textContent=await r.text();loadSlots();}"
            "$('scan').onclick=async()=>{$('scanlist').textContent='Suche läuft ...';try{let r=await fetch('/wifi/scan');let j=await r.json();$('scanlist').textContent='';if(!j.networks.length){$('scanlist').textContent='Keine WLAN-Netze gefunden.';return;}"
            "j.networks.forEach(n=>{let b=document.createElement('button');b.type='button';b.textContent=n.ssid+' ('+n.rssi+' dBm)'+(n.open?' · offen':'');b.onclick=()=>{$('ssid').value=n.ssid;$('password').focus();};$('scanlist').appendChild(b);});}catch(e){$('scanlist').textContent='WLAN-Suche fehlgeschlagen.';}};"
            "$('wifiForm').onsubmit=async e=>{e.preventDefault();let r=await fetch('/wifi/store',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(e.target))});$('message').textContent=await r.text();$('password').value='';loadSlots();};"
            "async function status(){try{let r=await fetch('/wifi/status');let j=await r.json();$('status').textContent=j.connected?'Verbunden: '+j.ssid+' · '+j.ip:'Nicht mit Router verbunden · Setup-Seite bleibt erreichbar';}catch(e){}}"
            "loadSlots();status();setInterval(status,3000);</script></main></body></html>");
  server_.send(200, "text/html; charset=utf-8", page);
}

void WifiClient::handleSplashUpload() {
  HTTPUpload& upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (splashUploadFile_) splashUploadFile_.close();
    SplashService::discardTemporary();
    splashUploadBytes_ = 0;
    splashUploadFailed_ = false;
    splashUploadSucceeded_ = false;
    splashUploadMessage_ = "";
    String filename = upload.filename;
    filename.toLowerCase();
    if (!filename.endsWith(".png")) {
      splashUploadFailed_ = true;
      splashUploadMessage_ = "Bitte eine PNG-Datei auswählen.";
      return;
    }
    splashUploadFile_ = LittleFS.open(SplashService::TEMP_PATH, "w");
    if (!splashUploadFile_) {
      splashUploadFailed_ = true;
      splashUploadMessage_ = "LittleFS ist nicht verfügbar oder die temporäre Datei kann nicht angelegt werden.";
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (splashUploadFailed_ || !splashUploadFile_) return;
    if (splashUploadBytes_ + upload.currentSize > SplashService::MAX_FILE_SIZE) {
      splashUploadFailed_ = true;
      splashUploadMessage_ = "Das Bootbild darf maximal 512 KB groß sein.";
      splashUploadFile_.close();
      SplashService::discardTemporary();
      return;
    }
    const size_t written = splashUploadFile_.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      splashUploadFailed_ = true;
      splashUploadMessage_ = "Fehler beim Schreiben der PNG-Datei in LittleFS.";
      splashUploadFile_.close();
      SplashService::discardTemporary();
      return;
    }
    splashUploadBytes_ += written;
  } else if (upload.status == UPLOAD_FILE_END) {
    if (splashUploadFile_) {
      splashUploadFile_.flush();
      splashUploadFile_.close();
    }
    if (!splashUploadFailed_) {
      if (!SplashService::validate(SplashService::TEMP_PATH, 320, 170)) {
        splashUploadFailed_ = true;
        splashUploadMessage_ = "Ungültige PNG-Datei. Erforderlich sind exakt 320 × 170 Pixel und höchstens 512 KB.";
        SplashService::discardTemporary();
      } else if (!SplashService::install()) {
        splashUploadFailed_ = true;
        splashUploadMessage_ = "Das neue Bootbild konnte nicht sicher installiert werden; das bisherige Bild bleibt erhalten.";
        SplashService::discardTemporary();
      } else {
        splashUploadSucceeded_ = true;
        splashUploadMessage_ = "Bootbild erfolgreich gespeichert. Es wird beim nächsten Neustart angezeigt.";
      }
    }
    BootDiagnostics::printf("[SPLASH] upload bytes=%u result=%s",
      static_cast<unsigned>(splashUploadBytes_), splashUploadSucceeded_ ? "success" : "failed");
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (splashUploadFile_) splashUploadFile_.close();
    SplashService::discardTemporary();
    splashUploadFailed_ = true;
    splashUploadMessage_ = "Upload abgebrochen.";
  }
}

void WifiClient::handleSplashUploadComplete() {
  if (splashUploadSucceeded_) {
    server_.send(200, "text/html; charset=utf-8",
      "<meta charset='utf-8'><meta http-equiv='refresh' content='3;url=/'><p>Bootbild erfolgreich gespeichert. Die bisherige Anzeige bleibt bis zum Neustart aktiv.</p><p><a href='/'>Zurück</a></p>");
  } else {
    String message = htmlEscape(splashUploadMessage_.length() ? splashUploadMessage_ : String("Upload fehlgeschlagen."));
    server_.send(400, "text/html; charset=utf-8",
      "<meta charset='utf-8'><p>" + message + "</p><p><a href='/'>Zurück zur Konfiguration</a></p>");
  }
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


String WifiClient::jsonEscape(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i=0;i<value.length();++i) {
    const uint8_t ch=static_cast<uint8_t>(value[i]);
    if(ch=='"'||ch=='\\'){out+='\\';out+=static_cast<char>(ch);}
    else if(ch=='\n')out+=F("\\n"); else if(ch=='\r')out+=F("\\r"); else if(ch=='\t')out+=F("\\t");
    else if(ch<0x20)out+=' '; else out+=static_cast<char>(ch);
  }
  return out;
}
void WifiClient::handleWifiScan() {
  if(!started_||mode_==Mode::OFF){server_.send(503,"application/json","{\"error\":\"WLAN aus\",\"networks\":[]}");return;}
  const int16_t count=WiFi.scanNetworks(false,false,false,120);
  if(count<0){WiFi.scanDelete();server_.send(503,"application/json","{\"error\":\"Scan fehlgeschlagen\",\"networks\":[]}");return;}
  String json="{\"networks\":["; bool first=true;
  for(int16_t i=0;i<count;++i){String name=WiFi.SSID(static_cast<uint8_t>(i));if(!name.length())continue;
    if(!first)json+=',';first=false;json+="{\"ssid\":\"";json+=jsonEscape(name);json+="\",\"rssi\":";
    json+=String(WiFi.RSSI(static_cast<uint8_t>(i)));json+=",\"open\":";
    json+=WiFi.encryptionType(static_cast<uint8_t>(i))==WIFI_AUTH_OPEN?"true":"false";json+='}';}
  json+="]}";WiFi.scanDelete();server_.send(200,"application/json",json);
}
void WifiClient::handleWifiStore() {
  const String ssid=server_.arg("ssid"), password=server_.arg("password"), slotArg=server_.arg("slot");
  const int slot=slotArg.toInt();
  if(!ssid.length()||ssid.length()>32||password.length()>63||!slotArg.length()||slot<1||slot>NETWORK_COUNT){
    server_.send(400,"text/plain; charset=utf-8","Bitte SSID, Passwort und Speicherplatz 1–3 angeben.");return;}
  const uint8_t index=static_cast<uint8_t>(slot-1);ssid.toCharArray(ssid_[index],sizeof(ssid_[index]));password.toCharArray(password_[index],sizeof(password_[index]));
  if(!saveNetworks()){BootDiagnostics::printf("[WIFI][STORE] slot=%d save FAILED",slot);server_.send(500,"text/plain; charset=utf-8","Speichern fehlgeschlagen: NVS nicht verfügbar.");return;}
  activeNetwork_=index;
  BootDiagnostics::printf("[WIFI][STORE] slot=%d saved ssid_len=%u password_present=%u station_enabled=%u",
    slot, static_cast<unsigned>(ssid.length()), password.length() ? 1U : 0U, stationEnabled() ? 1U : 0U);
  server_.send(200,"text/plain; charset=utf-8","WLAN in Platz "+String(slot)+" gespeichert. Verbindungsaufbau läuft.");
  if(stationEnabled()){
    WiFi.disconnect(false,false);
    const wl_status_t beginStatus=WiFi.begin(ssid_[index],password_[index]);
    lastAttemptMs_=millis();
    BootDiagnostics::printf("[WIFI][ATTEMPT] source=web-store slot=%d begin_status=%s(%d)",slot,wifiStatusName(beginStatus),static_cast<int>(beginStatus));
  }
}
void WifiClient::handleWifiClear() {
  const String slotArg=server_.arg("slot");const int slot=slotArg.toInt();
  if(!slotArg.length()||slot<1||slot>NETWORK_COUNT){server_.send(400,"text/plain; charset=utf-8","Ungültiger Speicherplatz.");return;}
  const uint8_t index=static_cast<uint8_t>(slot-1);const bool wasActive=index==activeNetwork_;
  ssid_[index][0]='\0';password_[index][0]='\0';
  if(!saveNetworks()){BootDiagnostics::printf("[WIFI][CLEAR] slot=%d save FAILED",slot);server_.send(500,"text/plain; charset=utf-8","Löschen fehlgeschlagen: NVS nicht verfügbar.");return;}
  BootDiagnostics::printf("[WIFI][CLEAR] slot=%d was_active=%u",slot,wasActive ? 1U : 0U);
  server_.send(200,"text/plain; charset=utf-8","WLAN-Platz "+String(slot)+" wurde gelöscht.");
  if(wasActive&&stationEnabled()){WiFi.disconnect(false,false);connectNextNetwork();}
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
  server_.send(200, "text/html; charset=utf-8",
    "<meta charset='utf-8'><meta http-equiv='refresh' content='2;url=/'><p>Wetter-Standort gespeichert. Die Wetterdaten werden neu geladen.</p>");
}

void WifiClient::handleStatus() {
  const bool isConnected=connected();
  String json="{\"connected\":";json+=isConnected?"true":"false";
  json+=",\"ssid\":\"";if(isConnected)json+=jsonEscape(WiFi.SSID());
  json+="\",\"ip\":\"";json+=isConnected?WiFi.localIP().toString():String("10.1.1.1");
  json+="\",\"slots\":[";
  for(uint8_t i=0;i<NETWORK_COUNT;++i){if(i)json+=',';json+='"';json+=jsonEscape(String(ssid_[i]));json+='"';}
  json+="]}";server_.send(200,"application/json",json);
}

void WifiClient::connectNextNetwork() {
  if(!stationEnabled()) {
    BootDiagnostics::write("[WIFI][FALLBACK] skipped: station mode disabled");
    return;
  }
  const uint8_t previous=activeNetwork_;
  BootDiagnostics::printf("[WIFI][FALLBACK] begin previous_slot=%u status=%s",
    static_cast<unsigned>(previous + 1), wifiStatusName(WiFi.status()));
  for(uint8_t offset=1;offset<=NETWORK_COUNT;++offset){
    const uint8_t candidate=static_cast<uint8_t>((previous+offset)%NETWORK_COUNT);
    if(!ssid_[candidate][0]) {
      BootDiagnostics::printf("[WIFI][FALLBACK] slot=%u skipped empty", static_cast<unsigned>(candidate + 1));
      continue;
    }
    activeNetwork_=candidate;
    // Explicitly tear down the previous STA attempt before switching slots.
    // Keep the setup AP running (eraseap=false, wifioff=false).
    const bool disconnected = WiFi.disconnect(false, false);
    delay(50);
    const wl_status_t beginStatus = WiFi.begin(ssid_[activeNetwork_],password_[activeNetwork_]);
    lastAttemptMs_=millis();
    BootDiagnostics::printf("[WIFI][FALLBACK] selected slot=%u disconnect_result=%u begin_status=%s(%d) AP=%s",
      static_cast<unsigned>(activeNetwork_ + 1), disconnected ? 1U : 0U,
      wifiStatusName(beginStatus), static_cast<int>(beginStatus), apActive_ ? "ON" : "OFF");
    Serial.printf("[WIFI] trying saved network slot %u\\n",static_cast<unsigned>(activeNetwork_+1));return;
  }
  BootDiagnostics::write("[WIFI][FALLBACK] exhausted all three slots; no configured SSID available");
  Serial.println("[WIFI] no configured SSID found");
}
void WifiClient::update() {
  if (!started_) return;
  if (serverStarted_ && mode_ != Mode::SYNC_ONLY) server_.handleClient();
  const wl_status_t status = WiFi.status();
  const uint32_t now = millis();
  if (status != lastStatus_) {
    const wl_status_t previousStatus = lastStatus_;
    lastStatus_ = status;
    BootDiagnostics::printf("[WIFI][STATE] %s(%d)->%s(%d) slot=%u attempt_age_ms=%lu AP=%s",
      wifiStatusName(previousStatus), static_cast<int>(previousStatus), wifiStatusName(status), static_cast<int>(status),
      static_cast<unsigned>(activeNetwork_ + 1), static_cast<unsigned long>(now - lastAttemptMs_), apActive_ ? "ON" : "OFF");
    if (status == WL_CONNECTED) {
      BootDiagnostics::printf("[WIFI][CONNECTED] slot=%u ip=%s rssi=%d channel=%u heap=%u",
        static_cast<unsigned>(activeNetwork_ + 1), WiFi.localIP().toString().c_str(), WiFi.RSSI(),
        static_cast<unsigned>(WiFi.channel()), static_cast<unsigned>(ESP.getFreeHeap()));
    }
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
    BootDiagnostics::printf("[WIFI][TIMEOUT] slot=%u status=%s(%d) elapsed_ms=%lu retry_after_ms=%lu",
      static_cast<unsigned>(activeNetwork_ + 1), wifiStatusName(status), static_cast<int>(status),
      static_cast<unsigned long>(now - lastAttemptMs_), static_cast<unsigned long>(RETRY_INTERVAL_MS));
    lastAttemptMs_ = now;
    connectNextNetwork();
  }
  if (status != WL_CONNECTED && now - lastLogMs_ >= 15000) {
    lastLogMs_ = now;
    BootDiagnostics::printf("[WIFI][WAIT] status=%s(%d) active_slot=%u AP=%s ip=%s elapsed_since_attempt_ms=%lu",
      wifiStatusName(status), static_cast<int>(status), static_cast<unsigned>(activeNetwork_ + 1),
      apActive_ ? "ON" : "OFF", apActive_ ? WiFi.softAPIP().toString().c_str() : "-",
      static_cast<unsigned long>(now - lastAttemptMs_));
    Serial.printf("[WIFI] waiting for router; setup page http://10.1.1.1/ (status=%d)\\n",
                  static_cast<int>(status));
  }
}

bool WifiClient::connected() const {
  return started_ && stationEnabled() && WiFi.status() == WL_CONNECTED;
}
