#include "NewsService.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>
#include "../system/BootDiagnostics.h"

namespace {
// Arduino-ESP32 3.3.x exposes the built-in ESP-IDF certificate bundle through
// esp_crt_bundle_attach(). Keep this tiny adapter local to the NewsService;
// it uses the same protected SSL context/helper as NetworkClientSecure itself.
class NewsFeedTlsClient : public WiFiClientSecure {
public:
  void useDefaultCertificateBundle() {
    attach_ssl_certificate_bundle(sslclient.get(), true);
  }
};

constexpr size_t MAX_RESPONSE_BYTES = 24576;
constexpr uint32_t CONNECT_TIMEOUT_MS = 5000;
constexpr uint32_t READ_TIMEOUT_MS = 5000;
constexpr uint32_t TASK_STACK_WORDS = 8192;
constexpr UBaseType_t TASK_PRIORITY = 1;
constexpr BaseType_t TASK_CORE = 0;
NewsService* workerInstance = nullptr;
portMUX_TYPE snapshotMux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE configMux = portMUX_INITIALIZER_UNLOCKED;
bool workerRunning = false;
bool refreshRequested = false;
bool workerWifiConnected = false;
uint32_t lastAttemptMs = 0;

String xmlDecode(String value) {
  value.replace("&amp;", "&"); value.replace("&lt;", "<");
  value.replace("&gt;", ">"); value.replace("&quot;", "\""); value.replace("&apos;", "'");
  value.replace("&#39;", "'"); value.replace("&nbsp;", " ");
  value.replace("&ndash;", "-"); value.replace("&mdash;", "-");
  value.replace("&euro;", "EUR");
  for (uint8_t i=0;i<8;i++) {
    int p=value.indexOf("&#");
    if(p<0) break;
    int end=value.indexOf(';',p);
    if(end<0) break;
    String token=value.substring(p+2,end);
    char* tail=nullptr;
    const bool hexadecimal=token.length()>1 && (token[0]=='x' || token[0]=='X');
    long code=strtol(hexadecimal?token.c_str()+1:token.c_str(),&tail,hexadecimal?16:10);
    if(tail && *tail=='\0' && code>=32 && code<=0x10FFFF &&
       !(code>=0xD800 && code<=0xDFFF)) {
      String replacement;
      if(code<=0x7F) replacement+=(char)code;
      else if(code<=0x7FF) {
        replacement+=(char)(0xC0|(code>>6));
        replacement+=(char)(0x80|(code&0x3F));
      } else if(code<=0xFFFF) {
        replacement+=(char)(0xE0|(code>>12));
        replacement+=(char)(0x80|((code>>6)&0x3F));
        replacement+=(char)(0x80|(code&0x3F));
      } else {
        replacement+=(char)(0xF0|(code>>18));
        replacement+=(char)(0x80|((code>>12)&0x3F));
        replacement+=(char)(0x80|((code>>6)&0x3F));
        replacement+=(char)(0x80|(code&0x3F));
      }
      value.replace(value.substring(p,end+1),replacement);
    } else value.remove(p,end-p+1);
  }
  return value;
}
String cleanTitle(String value) {
  value.replace("<![CDATA[", "");
  value.replace("]]>", "");
  bool inTag=false; String out; out.reserve(value.length());
  for(size_t i=0;i<value.length();++i) {
    char c=value[i];
    if(c=='<') { inTag=true; continue; }
    if(c=='>') { inTag=false; continue; }
    if(!inTag && (c=='\r'||c=='\n'||c=='\t')) c=' ';
    if(!inTag && (uint8_t)c>=32) out+=c;
  }
  out=xmlDecode(out); out.trim();
  while(out.indexOf("  ")>=0) out.replace("  "," ");
  return out;
}
bool extractTag(const String& xml,int from,const char* tag,String& value,int& next) {
  String open=String("<")+tag, close=String("</")+tag+">";
  int a=xml.indexOf(open,from); if(a<0) return false;
  a=xml.indexOf('>',a); if(a<0) return false; ++a;
  int b=xml.indexOf(close,a); if(b<0) return false;
  value=xml.substring(a,b); next=b+close.length(); return true;
}
void addTitle(NewsService::Snapshot& snap,const String& raw,uint8_t source) {
  if(snap.count>=NewsService::MAX_HEADLINES) return;
  String title=cleanTitle(raw);
  if(title.length()<4) return;
  for(uint8_t i=0;i<snap.count;i++) if(title.equals(String(snap.items[i].title))) return;
  title.trim();
  size_t n=title.length(); if(n>=NewsService::MAX_HEADLINE_BYTES) n=NewsService::MAX_HEADLINE_BYTES-1;
  while(n && (((uint8_t)title[n]&0xC0)==0x80)) --n;
  memcpy(snap.items[snap.count].title,title.c_str(),n);
  snap.items[snap.count].title[n]='\0';
  snap.items[snap.count].sourceIndex=source; ++snap.count;
}
void parseFeed(const String& xml,uint8_t source,NewsService::Snapshot& snap) {
  int pos=0,next=0; String block,title;
  // RSS 2.0 <item>, RSS 1.0 <item>, and Atom <entry> are supported.
  while(snap.count<NewsService::MAX_HEADLINES) {
    int item=xml.indexOf("<item",pos), entry=xml.indexOf("<entry",pos);
    if(item<0 && entry<0) break;
    bool atom=(entry>=0 && (item<0 || entry<item));
    int start=atom?entry:item;
    int end=xml.indexOf(atom?"</entry>":"</item>",start);
    if(end<0) break;
    block=xml.substring(start,end+ (atom?8:7));
    if(extractTag(block,0,"title",title,next)) addTitle(snap,title,source);
    pos=end+(atom?8:7);
  }
}
bool fetchOne(const char* url,uint8_t source,NewsService::Snapshot& snap) {
  BootDiagnostics::printf("[NEWS] fetch start feed=%u", static_cast<unsigned>(source + 1));

  // Record only the host (never the full URL, which may contain private tokens).
  const char* hostStart = strstr(url, "://");
  hostStart = hostStart ? hostStart + 3 : url;
  const char* hostEnd = strpbrk(hostStart, "/:?#");
  const size_t hostLength = hostEnd ? static_cast<size_t>(hostEnd - hostStart) : strlen(hostStart);
  char host[96] = {};
  const size_t hostCopy = hostLength < sizeof(host) - 1 ? hostLength : sizeof(host) - 1;
  memcpy(host, hostStart, hostCopy);
  BootDiagnostics::printf("[NEWS] target host=%s wifi_status=%d rssi=%d",
    host, static_cast<int>(WiFi.status()),
    WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);

  IPAddress resolvedAddress;
  const uint32_t dnsStartMs = millis();
  const int dnsResult = WiFi.hostByName(host, resolvedAddress);
  BootDiagnostics::printf("[NEWS] DNS result=%d elapsed_ms=%lu ip=%s",
    dnsResult, static_cast<unsigned long>(millis() - dnsStartMs),
    dnsResult == 1 ? resolvedAddress.toString().c_str() : "unresolved");
  if (dnsResult != 1) {
    BootDiagnostics::write("[NEWS] DNS resolution failed; skipping HTTPS request");
    return false;
  }

  NewsFeedTlsClient tls;
  // TEMPORARY DIAGNOSTIC TEST: keep HTTPS encryption, but skip certificate
  // verification to determine whether CA-bundle validation causes GET to fail.
  BootDiagnostics::write("[NEWS] TEST MODE: TLS certificate verification disabled");
  tls.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(READ_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setRedirectLimit(3);
  http.setUserAgent("ATS-Mini-OS-Newsfeed/1.0");
  if(!http.begin(tls,url)) { BootDiagnostics::printf("[NEWS] HTTP begin failed feed=%u", static_cast<unsigned>(source + 1)); return false; }
  BootDiagnostics::printf("[NEWS] HTTP begin OK feed=%u; before GET", static_cast<unsigned>(source + 1));
  const uint32_t getStartMs = millis();
  int code=http.GET();
  const String errorText = code < 0 ? HTTPClient::errorToString(code) : String("HTTP response");
  BootDiagnostics::printf("[NEWS] GET returned feed=%u http_code=%d error=%s elapsed_ms=%lu wifi_status=%d",
    static_cast<unsigned>(source + 1), code, errorText.c_str(),
    static_cast<unsigned long>(millis() - getStartMs),
    static_cast<int>(WiFi.status()));
  if(code!=HTTP_CODE_OK) { http.end(); return false; }
  int length=http.getSize();
  if(length>static_cast<int>(MAX_RESPONSE_BYTES)) { http.end(); return false; }
  WiFiClient* stream=http.getStreamPtr();
  String body; body.reserve(length>0?length:4096);
  uint32_t lastData=millis();
  while(http.connected() && (length>0 ? (int)body.length()<length : millis()-lastData<READ_TIMEOUT_MS)) {
    size_t available=stream->available();
    if(available) {
      while(available-- && body.length()<MAX_RESPONSE_BYTES) body+=(char)stream->read();
      lastData=millis();
      if(body.length()>=MAX_RESPONSE_BYTES) break;
    } else vTaskDelay(pdMS_TO_TICKS(10));
  }
  http.end();
  if(body.length()<32 || body.length()>=MAX_RESPONSE_BYTES) return false;
  const uint8_t before=snap.count;
  parseFeed(body,source,snap);
  return snap.count>before;
}
void newsWorker(void*) {
  BootDiagnostics::write("[NEWS] worker task entered");
  NewsService* service=workerInstance;
  for(;;) {
    bool connected=false, requested=false;
    portENTER_CRITICAL(&configMux);
    connected=workerWifiConnected; requested=refreshRequested; refreshRequested=false;
    portEXIT_CRITICAL(&configMux);
    if(connected && service) {
      NewsService::Configuration cfg=service->configuration();
      const uint32_t interval=(uint32_t)cfg.refreshMinutes*60000UL;
      if(cfg.enabled && (requested || !lastAttemptMs || millis()-lastAttemptMs>=interval)) {
        BootDiagnostics::printf("[NEWS] refresh triggered requested=%s interval_min=%u", requested ? "yes" : "no", static_cast<unsigned>(cfg.refreshMinutes));
        lastAttemptMs=millis();
        NewsService::Snapshot fresh;
        fresh.fetchedAtMs=millis();
        for(uint8_t i=0;i<NewsService::MAX_FEEDS && fresh.count<NewsService::MAX_HEADLINES;i++) {
          if(cfg.urls[i][0]) fetchOne(cfg.urls[i],i,fresh);
          vTaskDelay(pdMS_TO_TICKS(20));
        }
        if(fresh.count) { fresh.hasData=true; fresh.stale=false; service->publish(fresh); }
        else service->markStale();
      }
    }
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
}

bool NewsService::validFeedUrl(const char* url) {
  if(!url || !*url) return false;
  size_t len=strnlen(url,MAX_URL_BYTES);
  if(len>=MAX_URL_BYTES || strncmp(url,"https://",8)!=0 || len<=8) return false;
  for(size_t i=0;i<len;i++) if((uint8_t)url[i]<=32) return false;
  return true;
}
NewsService::Configuration NewsService::configuration() const { Configuration copy; portENTER_CRITICAL(&configMux); copy=configuration_; portEXIT_CRITICAL(&configMux); return copy; }
void NewsService::setConfiguration(const Configuration& config) {
  Configuration next{}; next.enabled=config.enabled;
  next.refreshMinutes=constrain(config.refreshMinutes,MIN_REFRESH_MINUTES,MAX_REFRESH_MINUTES);
  for(uint8_t i=0;i<MAX_FEEDS;i++) if(validFeedUrl(config.urls[i])) strlcpy(next.urls[i],config.urls[i],MAX_URL_BYTES);
  portENTER_CRITICAL(&configMux); configuration_=next; refreshRequested=true; portEXIT_CRITICAL(&configMux);
}
void NewsService::publish(const Snapshot& updated) {
  Snapshot next=updated; if(next.count>MAX_HEADLINES) next.count=MAX_HEADLINES;
  next.hasData=next.count>0; next.stale=!next.hasData;
  portENTER_CRITICAL(&snapshotMux); snapshot_=next; portEXIT_CRITICAL(&snapshotMux);
}
void NewsService::markStale() { portENTER_CRITICAL(&snapshotMux); snapshot_.stale=true; portEXIT_CRITICAL(&snapshotMux); }
NewsService::Snapshot NewsService::snapshot() const { Snapshot copy; portENTER_CRITICAL(&snapshotMux); copy=snapshot_; portEXIT_CRITICAL(&snapshotMux); return copy; }
void NewsService::begin() {
  BootDiagnostics::write("[NEWS] begin entered");
  if(workerRunning) { BootDiagnostics::write("[NEWS] worker already running"); return; }
  workerInstance=this;
  if(xTaskCreatePinnedToCore(newsWorker,"NewsFeed",TASK_STACK_WORDS,nullptr,TASK_PRIORITY,nullptr,TASK_CORE)==pdPASS) { workerRunning=true; BootDiagnostics::write("[NEWS] worker task created"); }
  else BootDiagnostics::write("[NEWS] ERROR: worker task creation failed");
}
void NewsService::update(bool wifiConnected) {
  portENTER_CRITICAL(&configMux); workerWifiConnected=wifiConnected; portEXIT_CRITICAL(&configMux);
}
