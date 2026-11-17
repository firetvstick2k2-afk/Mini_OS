#include <Arduino.h>
#include <esp_system.h>
#include <LovyanGFX.hpp>
#include <LittleFS.h>
#include <time.h>
#include <math.h>
#include "src/radio/RadioCore.h"
#include "src/radio/DecoderFrequencyList.h"
#include "src/radio/MemoryService.h"
#include "src/radio/ScanService.h"
#include "src/radio/SeekService.h"
#include "src/system/PerformanceManager.h"
#include "src/system/BootDiagnostics.h"
#include "src/system/ClockService.h"
#include "src/system/MoonCalendar.h"
#include "src/eibi/EiBiService.h"
#include "src/display/DisplayDriver.h"
#include "src/input/EncoderInput.h"
#include "src/input/FrequencyKeypad.h"
#include "src/network/WifiClient.h"
#include "src/news/NewsService.h"
#include "src/weather/WeatherService.h"
#include "src/logo/StationLogoClient.h"
#include "src/system/PartitionDiagnostics.h"
#include "src/storage/StorageDiscovery.h"
#include "src/storage/FileManager.h"
#include "src/settings/SettingsStore.h"
#include "src/menus/MenuSSB.h"
#include "src/tuner/SsbPatchManager.h"
#include "src/audio/AudioCaptureService.h"
#include "src/cw/CwDecoderService.h"
#include "src/digital/ftx/FtxDecoderService.h"

namespace {
constexpr uint32_t SERIAL_BAUD = 115200;


constexpr uint32_t DEFAULT_FM_FREQUENCY_KHZ = 95400;
PerformanceManager performance;
ClockService systemClock;
EiBiService eibi;
RadioCore radio;
MemoryService memories;
ScanService scanner;
SeekService seeker;
uint8_t selectedMemorySlot = 0;
bool memoryEditActive = false;
uint8_t memoryEditCursor = 0;
char memoryEditName[16] = {};
DisplayDriver display;
SettingsStore settingsStore;
bool brightnessSavePending = false;
bool themeSavePending = false;
EncoderInput encoder;
FrequencyKeypad frequencyKeypad;
AudioCaptureService audioCapture;
CwDecoderService cwDecoder;
FtxDecoderService ftxDecoder;
WifiClient wifi;
NewsService newsService;
WeatherService weatherService;
StationLogoClient stationLogoClient;
uint32_t lastStatusMs = 0;
uint32_t lastObservedFmFrequencyKHz = DEFAULT_FM_FREQUENCY_KHZ;
uint32_t fmFrequencyChangedAtMs = 0;
bool fmFrequencySavePending = false;
uint8_t lastObservedVolume = 32;
uint32_t volumeChangedAtMs = 0;
bool volumeSavePending = false;
uint32_t encoderEventCount = 0;
const char* lastEncoderEvent = "WAIT";
bool menuOpen = false;
bool sliderAdjustmentActive = false;
bool bfoAdjustmentActive = false;
bool bfoSavePending = false;
bool frequencyCursorActive = false;
uint8_t frequencyCursorPos = 0;
uint8_t cursorBandIndex = 0xFF;
RadioModulation cursorModulation = static_cast<RadioModulation>(0xFF);
uint16_t cursorStepKHz = 0;
uint8_t menuPage = 0;
uint8_t menuSelection = 0;
FileManager::Entry fmEntries[FileManager::MAX_ENTRIES];
char fmPath[96] = "/";
char fmMessage[40] = "LittleFS bereit";
char fmSelectedPath[96] = "";
char fmSelectedName[48] = "";
uint8_t fmCount = 0, fmSelection = 0, fmActionSelection = 0, fmMode = 0;
uint8_t fmPendingAction = 0, fmEditCursor = 0;
char fmEditName[48] = "";
void updateLastFmFrequencyPersistence() {
  if (!radio.isReady()) return;
  const RadioSnapshot& state = radio.snapshot();
  if (state.modulation != RadioModulation::FM) {
    fmFrequencySavePending = false;
    return;
  }

  if (state.frequencyKHz != lastObservedFmFrequencyKHz) {
    lastObservedFmFrequencyKHz = state.frequencyKHz;
    fmFrequencyChangedAtMs = millis();
    fmFrequencySavePending = true;
  }

  // Debounce encoder tuning so NVS is written once after the user stops tuning.
  if (fmFrequencySavePending && millis() - fmFrequencyChangedAtMs >= 1000) {
    const bool saved = settingsStore.saveLastFmFrequencyKHz(lastObservedFmFrequencyKHz);
    Serial.printf("[SETTINGS] last FM frequency %.2f MHz %s\n",
      lastObservedFmFrequencyKHz / 1000.0f, saved ? "saved" : "save failed");
    fmFrequencySavePending = false;
  }
}

void updateVolumePersistence() {
  if (!radio.isReady()) return;
  const uint8_t currentVolume = radio.volume();
  if (currentVolume != lastObservedVolume) {
    lastObservedVolume = currentVolume;
    volumeChangedAtMs = millis();
    volumeSavePending = true;
  }
  if (volumeSavePending && millis() - volumeChangedAtMs >= 1000) {
    const bool saved = settingsStore.saveVolume(lastObservedVolume);
    Serial.printf("[SETTINGS] radio volume=%u/63 %s\n",
      static_cast<unsigned>(lastObservedVolume), saved ? "saved" : "save failed");
    volumeSavePending = false;
  }
}

void saveBrightnessIfPending() {
  if (!brightnessSavePending) return;
  if (settingsStore.saveDisplayBrightness(display.brightness())) {
    Serial.printf("[SETTINGS] display brightness saved: %u/255\n",
      static_cast<unsigned>(display.brightness()));
  } else {
    Serial.println("[SETTINGS] display brightness save failed; current session remains active.");
  }
  brightnessSavePending = false;
}
void saveThemeIfPending() {
  if (!themeSavePending) return;
  if (settingsStore.saveDisplayTheme(display.themeIndex())) {
    Serial.printf("[SETTINGS] display theme saved: %s\n", ThemeManager::at(display.themeIndex()).name);
  } else {
    Serial.println("[SETTINGS] display theme save failed; current session remains active.");
  }
  themeSavePending = false;
}
void closeMenuToRadio(const char* reason) {
  saveBrightnessIfPending();
  saveThemeIfPending();
  menuOpen = false;
  sliderAdjustmentActive = false;
  menuPage = 0;
  menuSelection = 0;
  Serial.printf("[MENU] returned to radio: %s\n", reason);
}
bool tuneDecoderPreset(uint8_t presetIndex) {
  if (presetIndex >= kDecoderFrequencyPresetCount) return false;
  const DecoderFrequencyPreset& preset = kDecoderFrequencyPresets[presetIndex];
  // The ALL band spans the SI4735 AM/SSB range and lets these presets cross
  // amateur, maritime and CB allocations without inheriting an old band limit.
  if (!radio.selectBand(1)) return false;
  if (!radio.setModulation(preset.modulation)) return false;
  if (!radio.setBandwidthChoice(preset.bandwidthChoice)) return false;
  if (!radio.tune(preset.frequencyKHz)) return false;
  display.notifyTuning();
  Serial.printf("[DECODER FREQ] %s -> %lu kHz mode=%u\n",
    preset.label, static_cast<unsigned long>(preset.frequencyKHz),
    static_cast<unsigned>(preset.modulation));
  return true;
}
uint32_t frequencyCursorStepKHz(uint8_t position, RadioModulation mode) {
  const int offset = mode == RadioModulation::AM ? 6 : 0;
  if (position < offset) return 1;
  uint32_t power = 1;
  for (int i=0;i<(position-offset)/2;++i) power*=10;
  return (position&1U)?5U*power:power;
}
void syncFrequencyCursor() {
  const RadioSnapshot& s=radio.snapshot();
  const uint8_t choice=radio.stepChoice();
  const uint16_t fmSteps[]={10,50,100,200,1000};
  const uint16_t amSteps[]={1,5,9,10};
  const uint8_t fmIndex=choice<5?choice:4;
  const uint8_t amIndex=choice<4?choice:3;
  const uint16_t step=s.modulation==RadioModulation::FM?fmSteps[fmIndex]:
    (s.modulation==RadioModulation::LSB||s.modulation==RadioModulation::USB?1000:amSteps[amIndex]);
  if(cursorBandIndex==radio.currentBandIndex()&&cursorModulation==s.modulation&&cursorStepKHz==step)return;
  cursorBandIndex=radio.currentBandIndex();cursorModulation=s.modulation;cursorStepKHz=step;
  frequencyCursorPos=static_cast<uint8_t>(max(0,static_cast<int>(log10f(step)*2.0f)+(s.modulation==RadioModulation::AM?6:0)));
  frequencyCursorActive=false;
}
uint8_t frequencyCursorMinimum() { return radio.snapshot().modulation==RadioModulation::FM ? 2 : 0; }
uint8_t frequencyCursorMaximum() {
  const RadioSnapshot& s=radio.snapshot();const int offset=s.modulation==RadioModulation::FM?-2:6;
  const uint32_t maximum=s.bandMaximumKHz?s.bandMaximumKHz:1;
  return static_cast<uint8_t>(max(0,static_cast<int>(log10f(maximum)*2.0f)+offset));
}
bool tuneFrequencyDigit(int16_t delta) {
  const RadioSnapshot& s=radio.snapshot();const uint32_t step=frequencyCursorStepKHz(frequencyCursorPos,s.modulation);
  if(s.modulation==RadioModulation::LSB||s.modulation==RadioModulation::USB){
    const int32_t hz=static_cast<int32_t>(step);
    const int32_t next=static_cast<int32_t>(radio.bfoOffsetHz())+static_cast<int32_t>(delta)*hz;
    if(!radio.setBfoOffsetHz(static_cast<int16_t>(constrain(next,-16000,16000))))return false;
    bfoSavePending=true;
  }else{
    const int64_t next=static_cast<int64_t>(s.frequencyKHz)+static_cast<int64_t>(delta)*step;
    const uint32_t target=static_cast<uint32_t>(constrain(next,static_cast<int64_t>(s.bandMinimumKHz),static_cast<int64_t>(s.bandMaximumKHz)));
    if(!radio.tune(target))return false;
  }
  display.notifyTuning();return true;
}
bool alignFrequencyCursor() {
  const RadioSnapshot& s=radio.snapshot();const uint32_t step=frequencyCursorStepKHz(frequencyCursorPos,s.modulation);
  if(s.modulation==RadioModulation::LSB||s.modulation==RadioModulation::USB){
    const int32_t hz=static_cast<int32_t>(step);
    const int32_t combined=static_cast<int32_t>(s.frequencyKHz*1000UL)+radio.bfoOffsetHz();
    const int32_t rem=((combined%hz)+hz)%hz;
    return radio.setBfoOffsetHz(static_cast<int16_t>(radio.bfoOffsetHz()-rem));
  }
  return radio.tune(constrain(s.frequencyKHz-s.frequencyKHz%step,s.bandMinimumKHz,s.bandMaximumKHz));
}
uint16_t nextSpectrumPeak(const ScanService& scan, uint16_t current, int8_t direction) {
  const uint16_t count=scan.count();
  if(count<3 || !direction) return current;
  uint8_t minR=255,maxR=0;
  for(uint16_t i=0;i<count;++i){const uint8_t v=scan.point(i).rssi;if(v<minR)minR=v;if(v>maxR)maxR=v;}
  const uint8_t threshold=static_cast<uint8_t>(minR+(static_cast<uint16_t>(maxR-minR)*5)/100);
  for(uint16_t offset=1;offset<count;++offset){
    int32_t candidate=static_cast<int32_t>(current)+static_cast<int32_t>(direction)*offset;
    while(candidate<0)candidate+=count;
    candidate%=count;
    if(candidate==0 || candidate==count-1) continue;
    const uint8_t v=scan.point(static_cast<uint16_t>(candidate)).rssi;
    if(v>=threshold && v>=scan.point(static_cast<uint16_t>(candidate-1)).rssi &&
       v>=scan.point(static_cast<uint16_t>(candidate+1)).rssi) return static_cast<uint16_t>(candidate);
  }
  uint16_t best=current;
  for(uint16_t offset=1;offset<count;++offset){
    int32_t candidate=static_cast<int32_t>(current)+static_cast<int32_t>(direction)*offset;
    while(candidate<0)candidate+=count;
    candidate%=count;
    if(scan.point(static_cast<uint16_t>(candidate)).rssi>scan.point(best).rssi) best=static_cast<uint16_t>(candidate);
  }
  return best;
}
void fmRefresh(){ fmCount=0; if(!FileManager::list(fmPath,fmEntries,FileManager::MAX_ENTRIES,fmCount)) snprintf(fmMessage,sizeof(fmMessage),"Ordner nicht lesbar"); }
void fmMessageSet(const char* m){ strncpy(fmMessage,m,sizeof(fmMessage)-1); fmMessage[sizeof(fmMessage)-1]=0; }
void fmParent(){ if(strcmp(fmPath,"/")==0)return; char* p=strrchr(fmPath,47); if(p==fmPath)fmPath[1]=0; else if(p)*p=0; fmSelection=0; fmRefresh(); }
void fmOpen(){ fmMode=0; fmSelection=0; fmActionSelection=0; fmMessageSet(""); fmRefresh(); menuPage=6; menuSelection=0; }
void fmChoose(uint8_t visual){ if(visual==0){ if(fmMode==3)return; fmParent(); return; } if(fmMode==0 && visual==fmCount+1){fmPendingAction=5;fmEditName[0]=0;fmEditCursor=0;fmMode=4;return;} if(fmMode==0 && visual==fmCount+2){menuOpen=false;menuPage=0;menuSelection=0;fmMessageSet("Dateimanager beendet");return;} uint8_t i=visual-1; if(i>=fmCount)return; if(fmEntries[i].directory){strncpy(fmPath,fmEntries[i].path,sizeof(fmPath)-1);fmPath[sizeof(fmPath)-1]=0;fmSelection=0;fmRefresh();} else {strncpy(fmSelectedPath,fmEntries[i].path,sizeof(fmSelectedPath)-1);fmSelectedPath[sizeof(fmSelectedPath)-1]=0;strncpy(fmSelectedName,fmEntries[i].name,sizeof(fmSelectedName)-1);fmSelectedName[sizeof(fmSelectedName)-1]=0;fmMode=1;fmActionSelection=0;} }
}


void setup() {
  Serial.begin(SERIAL_BAUD); delay(250); performance.begin();
  // Audio capture and digital decoder workers are initialized only on demand.
  systemClock.begin();
  const bool storageReady = StorageDiscovery::begin();
  const bool settingsReady = settingsStore.begin();
  BootDiagnostics::begin(settingsStore.bootLogEnabled(true));

  // Prepare the News cache folders immediately after LittleFS is mounted,
  // just like the station-logo folders. This is idempotent and never formats
  // or removes existing user data.
  if (storageReady) {
    bool extraReady = LittleFS.exists("/Extra") || LittleFS.mkdir("/Extra");
    bool newsReady = extraReady &&
      (LittleFS.exists("/Extra/News") || LittleFS.mkdir("/Extra/News"));
    BootDiagnostics::printf("[NEWS] cache directories %s",
      newsReady ? "ready (/Extra/News)" : "creation failed");
  } else {
    BootDiagnostics::write("[NEWS] cache directories skipped: LittleFS unavailable");
  }
  BootDiagnostics::printf("BOOT START reset_reason=%d free_heap=%u max_block=%u min_free=%u storage=%s partition=%s anchor=%s",
    static_cast<int>(esp_reset_reason()),
    static_cast<unsigned>(ESP.getFreeHeap()),
    static_cast<unsigned>(ESP.getMaxAllocHeap()),
    static_cast<unsigned>(ESP.getMinFreeHeap()),
    storageReady ? "READY" : "UNAVAILABLE",
    StorageDiscovery::partitionLabel(),
    StorageDiscovery::anchorPath());
  eibi.begin();
  FileManager::begin();
  stationLogoClient.begin();

  encoder.begin();
  Serial.printf("[SETTINGS] dedicated NVS %s\n", settingsReady ? "ready" : "unavailable; using defaults");
  const uint8_t savedBrightness = settingsStore.displayBrightness(180);
  const uint8_t savedTheme = settingsStore.displayTheme(0);
  const uint8_t savedFmLayout = settingsStore.displayFmLayout(0);
  const uint8_t savedAmLayout = settingsStore.displayAmLayout(0);
  const uint8_t savedCpuProfile = settingsStore.cpuProfile(0);
  const uint8_t savedPatchProfile = settingsStore.patchProfile(0);
  performance.setProfile(static_cast<CpuProfile>(savedCpuProfile));
  if (display.begin(savedBrightness)) {
    display.setTheme(savedTheme);
    display.setFmLayout(savedFmLayout);
    display.setAmLayout(savedAmLayout);
    Serial.printf("[DISPLAY] first UI initialized; brightness=%u/255\n",
      static_cast<unsigned>(display.brightness()));
  }
  const uint32_t startupFmFrequencyKHz =
    settingsStore.lastFmFrequencyKHz(DEFAULT_FM_FREQUENCY_KHZ);
  Serial.printf("[SETTINGS] startup FM frequency %.2f MHz (%s)\n",
    startupFmFrequencyKHz / 1000.0f,
    settingsStore.isReady() ? "restored/default" : "NVS unavailable; default");
  memories.begin();
  if (radio.begin(startupFmFrequencyKHz)) {
    lastObservedFmFrequencyKHz = radio.snapshot().frequencyKHz;
    radio.setPatchProfile(savedPatchProfile);
    if (radio.patchProfile() != savedPatchProfile) settingsStore.savePatchProfile(radio.patchProfile());
    Serial.printf("[PATCH] startup profile=%u (%s)\n", static_cast<unsigned>(radio.patchProfile()), SsbPatchManager::profileName(radio.patchProfile()));
    const uint8_t startupVolume = settingsStore.volume(32);
    if (radio.setVolume(startupVolume)) {
      lastObservedVolume = radio.volume();
      Serial.printf("[SETTINGS] startup volume=%u/63 (%s)\n",
        static_cast<unsigned>(lastObservedVolume),
        settingsStore.isReady() ? "restored/default" : "NVS unavailable; default");
    }
    const bool agcAuto = settingsStore.agcAutomatic(true);
    const uint8_t agcIndex = settingsStore.agcGainIndex(20);
    const uint8_t softMuteDb = settingsStore.softMuteAttenuationDb(8);
    const uint8_t avcGain = settingsStore.avcGain(90);
    const uint8_t attIndex = settingsStore.attenuationIndex(0);
    const uint8_t squelch = settingsStore.squelchThreshold(0);
    const bool squelchSnr = settingsStore.squelchUsesSnr(false);
    const uint8_t fmRegion = settingsStore.fmRegion(0);
    const uint8_t fmStereo = settingsStore.fmStereoMode(0);
    const uint8_t ssbBandwidth = settingsStore.ssbBandwidthChoice(4);
    const int16_t bfoOffset = settingsStore.bfoOffsetHz(0);
    if (agcAuto) radio.setAgcAutomatic(true);
    else radio.setAgcManualGain(agcIndex);
    radio.setSoftMuteAttenuation(softMuteDb);
    radio.setAvcGain(avcGain); // Remembered in FM, applied automatically in AM/SSB.
    radio.setAttenuationIndex(attIndex);
    radio.setSquelch(squelch, squelchSnr);
    radio.setFmRegion(fmRegion);
    radio.setFmStereoMode(fmStereo);
    radio.setSsbBandwidthChoice(ssbBandwidth);
    radio.setBfoOffsetHz(bfoOffset);
    Serial.printf("[SETTINGS] RX restored: AGC=%s/%u SoftMute=%u dB AVC=%u\n",
      agcAuto ? "AUTO" : "MANUAL", static_cast<unsigned>(agcIndex),
      static_cast<unsigned>(softMuteDb), static_cast<unsigned>(avcGain));
    Serial.printf("Radio ready: %.1f MHz\n",radio.snapshot().frequencyKHz/1000.0f);
  } else {
    Serial.println("[TUNER] init failed; check I2C, radio power and GPIO configuration.");
  }
  wifi.setNewsService(&newsService);
  wifi.setWeatherService(&weatherService);
  BootDiagnostics::printf("[BOOT][STAGE] before wifi.begin free_heap=%u max_block=%u", static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  wifi.begin();
  BootDiagnostics::printf("[BOOT][STAGE] after wifi.begin (deferred) free_heap=%u max_block=%u min_free=%u", static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
  // DAB Style and the TEF Newsfeed layout use online station/content services.
  // Activate networking automatically only when one of these layouts is selected.
  if (display.fmLayout() == 0 || display.fmLayout() == 3) wifi.ensureStarted();
  BootDiagnostics::printf("[BOOT][STAGE] setup complete free_heap=%u max_block=%u min_free=%u loop_stack_high_water=%u", static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  Serial.printf("CPU profile: %s\n",performance.profileName());
}
void loop() {
  static bool firstLoopDiagnostics = true;
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] entered first loop");
  performance.update();
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] performance.update done");
  wifi.update();
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] wifi.update done");
  newsService.update(wifi.connected() && wifi.dataServicesAllowed());
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] newsService.update done");
  weatherService.update(wifi.connected() && wifi.dataServicesAllowed());
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] weatherService.update done");
  systemClock.update(wifi.connected());
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] systemClock.update done");
  struct tm utcNow{};
  const bool utcValid = systemClock.getUtc(utcNow);
  eibi.update(wifi.connected() && wifi.dataServicesAllowed(), utcValid, utcNow, radio.snapshot().frequencyKHz);
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] eibi.update done");
  // WifiClient owns and services the local setup/status web server.
  encoder.update();
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] encoder.update done");
  EncoderEvent inputEvent;
  while (encoder.poll(inputEvent)) {
    ++encoderEventCount;
    switch (inputEvent.type) {
      case EncoderEventType::RotateClockwise:
      case EncoderEventType::RotateCounterClockwise:
      {
        lastEncoderEvent = inputEvent.type == EncoderEventType::RotateClockwise ? "CW" : "CCW";
        // Acceleration is reserved for live frequency tuning. Menus and BFO
        // adjustment always move one logical step per encoder detent.
        const int16_t rotationDelta = (menuOpen || bfoAdjustmentActive || frequencyCursorActive) && !inputEvent.buttonHeld
          ? (inputEvent.delta > 0 ? 1 : -1) : inputEvent.delta;
        if(inputEvent.buttonHeld&&!menuOpen&&!bfoAdjustmentActive){syncFrequencyCursor();frequencyCursorPos=static_cast<uint8_t>(constrain(static_cast<int>(frequencyCursorPos)-rotationDelta,static_cast<int>(frequencyCursorMinimum()),static_cast<int>(frequencyCursorMaximum())));frequencyCursorActive=true;} else if (menuOpen) {
          if(menuPage==23 && scanner.state()!=ScanService::State::Running && scanner.count()>0) {
            const uint16_t selected=nextSpectrumPeak(scanner,static_cast<uint16_t>(menuSelection),rotationDelta>0?1:-1);
            menuSelection=static_cast<uint8_t>(selected);
            if(scanner.point(selected).frequencyKHz && radio.tune(scanner.point(selected).frequencyKHz)) display.notifyTuning();
          }
          else if(menuPage==22){if(memoryEditActive){const char* chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-.";const size_t n=strlen(chars);const char ch=memoryEditName[memoryEditCursor];const char* q=strchr(chars,ch?ch:'A');int pos=q?static_cast<int>(q-chars):0;pos=(pos+rotationDelta)%static_cast<int>(n);if(pos<0)pos+=static_cast<int>(n);memoryEditName[memoryEditCursor]=chars[pos];memoryEditName[memoryEditCursor+1]='\0';}else{int v=(int)selectedMemorySlot+rotationDelta;v%=MemoryService::kSlotCount;if(v<0)v+=MemoryService::kSlotCount;selectedMemorySlot=(uint8_t)v;}}
          else if(menuPage==21){ if(radio.nextChannel(rotationDelta)) display.notifyTuning(); }
          else if(menuPage==19){ frequencyKeypad.rotate(rotationDelta); }
          else if(menuPage==6){ if(fmMode==1){int v=(int)fmActionSelection+rotationDelta;v%=6;if(v<0)v+=6;fmActionSelection=(uint8_t)v;} else if(fmMode==2)fmActionSelection=fmActionSelection?0:1; else if(fmMode==4){const char* chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 _-.";size_t n=strlen(chars);char ch=fmEditName[fmEditCursor];const char* q=strchr(chars,ch?ch:65);int pos=q?(int)(q-chars):0;pos=(pos+rotationDelta)%(int)n;if(pos<0)pos+=(int)n;fmEditName[fmEditCursor]=chars[pos];fmEditName[fmEditCursor+1]=0;} else {uint8_t n=fmCount+(fmMode==3?1:3);int v=(int)fmSelection+rotationDelta;v%=n;if(v<0)v+=n;fmSelection=(uint8_t)v;} }
          else if (sliderAdjustmentActive && ((menuPage == 2 && menuSelection == 1))) {
            const int16_t requested = static_cast<int16_t>(display.brightness()) + rotationDelta * 5;
            const uint8_t nextBrightness = static_cast<uint8_t>(constrain(requested, 10, 255));
            display.setBrightness(nextBrightness);
            brightnessSavePending = true;
            Serial.printf("[MENU] display brightness=%u/255 (pending save)\n",
              static_cast<unsigned>(display.brightness()));
          } else if (sliderAdjustmentActive && menuPage == 1 && menuSelection == 1) {
            const int16_t requested = static_cast<int16_t>(radio.volume()) + rotationDelta;
            const uint8_t nextVolume = static_cast<uint8_t>(constrain(requested, 0, 63));
            if (radio.setVolume(nextVolume)) {
              volumeSavePending = true;
              lastObservedVolume = radio.volume();
              volumeChangedAtMs = millis();
              Serial.printf("[MENU] volume=%u/63\n", static_cast<unsigned>(radio.volume()));
            }
          } else {
            const int16_t itemCount = menuPage == 0 ? 25 : menuPage == 26 ? (kDecoderFrequencyPresetCount + 1) : menuPage == 25 ? 19 : menuPage == 22 ? MemoryService::kSlotCount : menuPage == 18 ? 2 : menuPage == 20 ? 1 :
              menuPage == 2 ? 4 : menuPage == 4 ? 6 : menuPage == 5 ? 11 : menuPage == 17 ? 5 : menuPage == 13 ? 3 :
              ((menuPage == 14) ? 4 : (menuPage == 15) ? 5 :
              menuPage == 10 ? 4 : menuPage == 11 ?
              (radio.snapshot().modulation == RadioModulation::FM ? 10 : 14) :
              menuPage == 3 ? 8 : menuPage == 28 ? 2 : menuPage == 27 ? 4 :
              menuPage == 12 ? 2 : menuPage == 16 ?
              (radio.canUseModulation(RadioModulation::USB) ? 4 : 2) :
              menuPage == 7 ? radio.bandwidthChoiceCount()+1 :
              menuPage == 8 ? radio.bandCount()+1 :
              menuPage == 9 ? radio.stepChoiceCount()+1 : 3);
            int16_t next = (static_cast<int16_t>(menuSelection) + rotationDelta) % itemCount;
            if (next < 0) next += itemCount;
            menuSelection = static_cast<uint8_t>(next);
          }
        } else if (bfoAdjustmentActive) {
          const int32_t requested = static_cast<int32_t>(radio.bfoOffsetHz()) + rotationDelta * 10;
          const int16_t nextBfo = static_cast<int16_t>(constrain(requested, -16000, 16000));
          if (radio.setBfoOffsetHz(nextBfo)) {
            bfoSavePending = true;
            Serial.printf("[BFO] offset=%d Hz\n", static_cast<int>(radio.bfoOffsetHz()));
          }
        } else if(frequencyCursorActive){if(tuneFrequencyDigit(rotationDelta))Serial.printf("[FREQ CURSOR] pos=%u step=%lu kHz\n",frequencyCursorPos,static_cast<unsigned long>(frequencyCursorStepKHz(frequencyCursorPos,radio.snapshot().modulation)));} else if (radio.nextChannel(rotationDelta)) {
          display.notifyTuning();
          Serial.printf("[INPUT] tune step %s -> %.1f MHz\n",
            rotationDelta > 0 ? "CW" : "CCW",
            radio.snapshot().frequencyKHz / 1000.0f);
        }
        break;
      }
      case EncoderEventType::ShortPress:
        lastEncoderEvent = "SHORT";
        if (bfoAdjustmentActive) {
          bfoAdjustmentActive = false;
          if (bfoSavePending) {
            const bool saved = settingsStore.saveBfoOffsetHz(radio.bfoOffsetHz());
            Serial.printf("[SETTINGS] BFO=%d Hz %s\n", static_cast<int>(radio.bfoOffsetHz()), saved ? "saved" : "save failed");
            bfoSavePending = false;
          }
          Serial.println("[BFO] adjustment finished; encoder returned to tuning");
        } else if(frequencyCursorActive){if(alignFrequencyCursor())display.notifyTuning();frequencyCursorActive=false;if(bfoSavePending){const bool saved=settingsStore.saveBfoOffsetHz(radio.bfoOffsetHz());Serial.printf("[SETTINGS] cursor BFO=%d Hz %s\n",static_cast<int>(radio.bfoOffsetHz()),saved?"saved":"save failed");bfoSavePending=false;}} else if (!menuOpen) {
          menuOpen = true;
          sliderAdjustmentActive = false;
          menuPage = 0;
          menuSelection = 0;
          Serial.println("[MENU] opened");
        } else if (menuPage == 26) {
          if (menuSelection == 0) {
            menuPage = 0; menuSelection = 13;
          } else if (tuneDecoderPreset(static_cast<uint8_t>(menuSelection - 1))) {
            closeMenuToRadio("decoder frequency selected");
          } else {
            Serial.printf("[DECODER FREQ] tuning failed for selection %u\n", static_cast<unsigned>(menuSelection));
          }
        } else if (menuPage == 22) {
          if(memoryEditActive) {
            if(memoryEditCursor < sizeof(memoryEditName)-2) {
              ++memoryEditCursor;
              if(memoryEditCursor >= strlen(memoryEditName)) { memoryEditName[memoryEditCursor]='A'; memoryEditName[memoryEditCursor+1]='\0'; }
            }
          } else if (memories.entry(selectedMemorySlot).frequencyKHz) {
            if(memories.recall(selectedMemorySlot,radio)){display.notifyTuning();closeMenuToRadio("memory recalled");}
          } else if(memories.save(selectedMemorySlot,radio.snapshot(),radio.currentBandIndex())) Serial.printf("[MEMORY] saved slot %u\n",static_cast<unsigned>(selectedMemorySlot+1));
        } else if (menuPage == 23) {
          if(scanner.state()==ScanService::State::Running) scanner.stop(radio);
          else if(scanner.count()>0 && scanner.state()!=ScanService::State::Idle) {
            const uint16_t selected=static_cast<uint16_t>(menuSelection);
            if(selected<scanner.count() && radio.tune(scanner.point(selected).frequencyKHz)) {
              display.notifyTuning(); closeMenuToRadio("spectrum peak selected");
            }
          } else {
            menuSelection=0;
            scanner.start(radio);
          }
        } else if (menuPage == 24) {
          if(seeker.state()==SeekService::State::Running) seeker.stop(); else seeker.start(radio,1,12);
        } else if (menuPage == 28) {
          const FtxMode selectedMode = menuSelection == 1 ? FtxMode::FT4 : FtxMode::FT8;
          const bool entered = ftxDecoder.enter(selectedMode,
            wifi.isOff(), systemClock.synchronized(), audioCapture);
          Serial.printf("[FTX] %s %s (Wi-Fi off=%d, UTC sync=%d)\\n",
            selectedMode == FtxMode::FT4 ? "FT4" : "FT8",
            entered ? "active" : "not started",
            wifi.isOff(), systemClock.synchronized());
        } else if (menuPage == 21) {
          cwDecoder.exit(); menuPage = 0; menuSelection = 23;
          Serial.println("[CW] decoder page closed");
        } else if (menuPage == 19) {
          if (!frequencyKeypad.press(radio)) closeMenuToRadio("frequency keypad finished");
        } else if(menuPage==6){ if(fmMode==1){ if(fmActionSelection==0)fmMode=0; else if(fmActionSelection==1||fmActionSelection==2){fmPendingAction=fmActionSelection;fmMode=3;fmSelection=0;} else if(fmActionSelection==4){fmMode=2;fmActionSelection=0;} else {fmPendingAction=fmActionSelection==5?5:3; if(fmPendingAction==5)fmEditName[0]=0;else strncpy(fmEditName,fmSelectedName,sizeof(fmEditName)-1);fmEditName[sizeof(fmEditName)-1]=0;fmEditCursor=0;fmMode=4;} } else if(fmMode==2){if(fmActionSelection==1){bool ok=FileManager::removeFile(fmSelectedPath);fmMessageSet(ok?"Datei geloescht":"Loeschen fehlgeschlagen");fmRefresh();}fmMode=0;} else if(fmMode==4){if(fmEditCursor<sizeof(fmEditName)-2){++fmEditCursor;if(fmEditCursor>=strlen(fmEditName)){fmEditName[fmEditCursor]=65;fmEditName[fmEditCursor+1]=0;}}} else if(fmMode==3){if(fmSelection==0){const char* b=strrchr(fmSelectedPath,47);b=b?b+1:fmSelectedPath;char dest[96];snprintf(dest,sizeof(dest),"%s%s%s",fmPath,strcmp(fmPath,"/")==0?"":"/",b);bool ok=fmPendingAction==1?FileManager::copyFile(fmSelectedPath,dest):FileManager::moveFile(fmSelectedPath,dest);fmMessageSet(ok?"Erledigt":"Aktion fehlgeschlagen");fmMode=0;fmRefresh();}else {uint8_t idx=fmSelection-1;if(idx<fmCount&&fmEntries[idx].directory){strncpy(fmPath,fmEntries[idx].path,sizeof(fmPath)-1);fmPath[sizeof(fmPath)-1]=0;fmSelection=0;fmRefresh();}}}else fmChoose(fmSelection);
        } else if ((menuPage == 1 && menuSelection == 1) || (menuPage == 2 && menuSelection == 1)) {
          if (!sliderAdjustmentActive) {
            sliderAdjustmentActive = true;
            Serial.printf("[MENU] %s adjustment activated; turn to change, click to finish\n",
              menuPage == 1 ? "volume" : "brightness");
          } else {
            closeMenuToRadio("slider adjustment finished");
          }
        } else if ((menuPage==5 && menuSelection==4)) {fmOpen();
        } else if (menuPage == 0) {
            // ATS Mini Dev menu order is retained; the unsupported VHF spectrum slot now opens FT8/FT4.
            // Unsupported entries are marked with * in the renderer and stay inert.
            static const uint8_t targetPages[25] = {
              255, 16, 8, 1, 19, 255, 255, 255, 12, 9, 24,
              23, 22, 26, 25, 7, 11, 11, 11, 5, 28, 255, 18, 20, 21
            };
            const uint8_t selectedMainItem = menuSelection;
            const uint8_t targetPage = targetPages[selectedMainItem];
            if (selectedMainItem == 0) {
              closeMenuToRadio("main menu back");
            } else if (targetPage == 255) {
              Serial.printf("[MENU] '%u' is not implemented yet\n",
                static_cast<unsigned>(selectedMainItem));
            } else {
              menuPage = targetPage;
              if (menuPage == 18) wifi.ensureStarted();
              if (menuPage == 19) frequencyKeypad.open(radio.snapshot());
              else if (menuPage == 21) {
                const bool entered = cwDecoder.enter(audioCapture, wifi.isOff());
                if (!entered && !cwDecoder.wifiBlocked()) Serial.println("[CW] audio sampler unavailable");
                else Serial.printf("[CW] decoder %s; Wi-Fi mode=%u\n", entered ? "active" : "blocked (turn Wi-Fi OFF)", static_cast<unsigned>(wifi.mode()));
              }
              else if (menuPage == 8) menuSelection = static_cast<uint8_t>(radio.currentBandIndex() + 1);
              else if (menuPage == 11 && selectedMainItem == 16) {
                menuSelection = radio.snapshot().modulation == RadioModulation::FM ? 0 : 10;
              } else if (menuPage == 11 && selectedMainItem == 17) menuSelection = 6;
              else if (menuPage == 11 && selectedMainItem == 15) menuSelection = 1;
              else menuSelection = 0;
              Serial.printf("[MENU] opened selection page %u\n", static_cast<unsigned>(menuPage));
            }
        } else if (menuPage == 18) {
          menuPage = 0;
          menuSelection = 21;
          Serial.println("[WEATHER] report closed");
        } else if (menuPage == 20) {
          menuPage = 0;
          menuSelection = 22;
          Serial.println("[MOON] calendar closed");
        } else if (menuPage == 13 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 9;
        } else if (menuPage == 13 && menuSelection == 1) {
          menuPage = 14;
          menuSelection = display.fmLayout() == 2 ? 2 : 1;
        } else if (menuPage == 13 && menuSelection == 2) {
          menuPage = 15;
           menuSelection = display.amLayout() == 4 ? 2 : (display.amLayout() == 7 ? 3 : (display.amLayout() == 8 ? 4 : 1));
        } else if ((menuPage == 14 || menuPage == 15) && menuSelection == 0) {
          const bool returningFromFm = menuPage == 14;
          menuPage = 5;
          menuSelection = returningFromFm ? 5 : 6;
        } else if (menuPage == 14 && menuSelection > 0) {
          const uint8_t layout = menuSelection == 1 ? 0 : (menuSelection == 2 ? 2 : 3);
          display.setFmLayout(layout);
          if (layout == 0 || layout == 3) wifi.ensureStarted();
          const bool saved = settingsStore.saveDisplayFmLayout(layout);
          Serial.printf("[SETTINGS] FM layout=%s %s\n",
            layout == 3 ? "TEF6686 - Newsfeed" : (layout == 2 ? "TEF6686 -FM Version" : "DAB Style"), saved ? "saved" : "save failed");
          closeMenuToRadio("FM layout selected");
        } else if (menuPage == 15 && menuSelection > 0) {
          // Preserve the legacy layout IDs and include the independent TEF6686 SW renderer.
          static const uint8_t layoutIds[] = { 1, 4, 7, 8 };
          static const char* const layoutNames[] = {
            "HJB", "Decoder / Wasserfall", "TinyRadio", "TEF6686 - KW/MW/LW"
          };
          const uint8_t index = static_cast<uint8_t>(menuSelection - 1);
          if (index < sizeof(layoutIds) / sizeof(layoutIds[0])) {
            const uint8_t layout = layoutIds[index];
            display.setAmLayout(layout);
            const bool saved = settingsStore.saveDisplayAmLayout(layout);
            Serial.printf("[SETTINGS] LW/MW/KW layout=%s %s\n",
              layoutNames[index], saved ? "saved" : "save failed");
            closeMenuToRadio("LW/MW/KW layout selected");
          }
        } else if (menuPage == 5 && menuSelection == 10) {
          const bool nextEnabled = !BootDiagnostics::enabled();
          if (settingsStore.saveBootLogEnabled(nextEnabled)) {
            BootDiagnostics::setEnabled(nextEnabled);
            Serial.printf("[SETTINGS] Boot-Log %s\n", nextEnabled ? "EIN" : "AUS");
          } else {
            Serial.println("[SETTINGS] Boot-Log konnte nicht gespeichert werden");
          }
        } else if (menuPage == 5 && menuSelection == 9) {
          wifi.ensureStarted();
          menuPage = 4;
          menuSelection = static_cast<uint8_t>(wifi.mode()) + 1;
        } else if (menuPage == 4 && menuSelection == 0) {
          menuPage = 5;
          menuSelection = 9;
        } else if (menuPage == 4 && menuSelection > 0) {
          const WifiClient::Mode selectedMode = static_cast<WifiClient::Mode>(menuSelection - 1);
          if (wifi.setMode(selectedMode)) {
            Serial.printf("[WIFI] operating mode changed to %u\n", static_cast<unsigned>(wifi.mode()));
          } else {
            Serial.println("[WIFI] operating mode could not be saved");
          }
        } else if (menuPage == 5 && menuSelection == 8) {
          wifi.ensureStarted();
          const bool started = eibi.beginDownload(wifi.connected() && wifi.dataServicesAllowed());
          Serial.printf("[EIBI] download request %s: %s\n", started ? "accepted" : "rejected", eibi.statusText());
          closeMenuToRadio("EiBi schedule download");
        } else if (menuPage == 5 && menuSelection == 2) {
          menuPage = 17;
          menuSelection = static_cast<uint8_t>(performance.profile()) + 1;
        } else if (menuPage == 17 && menuSelection == 0) {
          menuPage = 5;
          menuSelection = 2;
        } else if (menuPage == 17 && menuSelection > 0) {
          const CpuProfile selected = static_cast<CpuProfile>(menuSelection - 1);
          if (performance.setProfile(selected)) {
            const bool saved = settingsStore.saveCpuProfile(menuSelection - 1);
            Serial.printf("[SETTINGS] CPU profile=%s %s\n", performance.profileName(), saved ? "saved" : "save failed");
            closeMenuToRadio("CPU profile selected");
          }
        } else if (menuPage == 5 && menuSelection == 1) {
          menuPage = 2;
          menuSelection = 0;
        } else if (menuPage == 5 && menuSelection == 6) {
          menuPage = 14;
          menuSelection = display.fmLayout() == 2 ? 2 : 1;
        } else if (menuPage == 5 && menuSelection == 7) {
          menuPage = 15;
           menuSelection = display.amLayout() == 4 ? 2 : (display.amLayout() == 7 ? 3 : (display.amLayout() == 8 ? 4 : 1));
        } else if (menuPage == 2 && menuSelection == 0) {
          menuPage = 5;
          menuSelection = 1;
        } else if (menuPage == 2 && menuSelection == 3) {
          menuPage = 10;
          menuSelection = static_cast<uint8_t>(display.themeIndex() + 1);
        } else if (menuPage == 10 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 9;
        } else if (menuPage == 10 && menuSelection > 0) {
          display.setTheme(static_cast<uint8_t>(menuSelection - 1));
          themeSavePending = true;
          closeMenuToRadio("theme selected");
        } else if (menuPage == 12 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 6;
        } else if (menuPage == 27 && menuSelection == 0) {
          menuPage = 3;
          menuSelection = 7;
        } else if (menuPage == 27 && menuSelection > 0 && menuSelection <= 3) {
          const uint8_t profile = static_cast<uint8_t>(menuSelection - 1);
          if (radio.setPatchProfile(profile)) {
            const uint8_t appliedProfile = radio.patchProfile();
            const bool saved = settingsStore.savePatchProfile(appliedProfile);
            Serial.printf("[PATCH] selected %s %s\n", SsbPatchManager::profileName(appliedProfile), saved ? "saved" : "save failed");
            closeMenuToRadio("DSP patch profile selected");
          } else Serial.println("[PATCH] profile selection rejected");
        } else if (menuPage == 12 && menuSelection == 1) {
          menuOpen = false;
          bfoAdjustmentActive = true;
          bfoSavePending = false;
          menuPage = 0;
          menuSelection = 0;
          Serial.printf("[BFO] adjustment active at %d Hz; rotate to change, click to finish\n", static_cast<int>(radio.bfoOffsetHz()));
        } else if (menuPage == MenuSSB::kPage && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 6;
        } else if (menuPage == MenuSSB::kPage && menuSelection > 0) {
          if (MenuSSB::apply(radio, menuSelection)) {
            const bool saved = settingsStore.saveBfoOffsetHz(radio.bfoOffsetHz());
            Serial.printf("[SETTINGS] BFO=%d Hz %s\n", static_cast<int>(radio.bfoOffsetHz()), saved ? "saved" : "save failed");
          } else Serial.println("[MENU] BFO change rejected (limit +/-16000 Hz)");
        } else if (menuPage == 3 && menuSelection == 6) {
          menuPage = 12; menuSelection = 0;
        } else if (menuPage == 3 && menuSelection == 7) {
          menuPage = 27; menuSelection = static_cast<uint8_t>(radio.patchProfile() + 1);
        } else if (menuPage == 3 && menuSelection == 5) {
          menuPage = 11;
          menuSelection = 0;
        } else if (menuPage == 25 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 13;
        } else if (menuPage == 25 && menuSelection >= 1 && menuSelection <= 7) {
          const uint8_t threshold = menuSelection == 1 ? 0 :
            (menuSelection <= 4 ? static_cast<uint8_t>(menuSelection * 10) :
             static_cast<uint8_t>((menuSelection - 3) * 5));
          const bool useSnr = menuSelection >= 5;
          if (radio.setSquelch(threshold, useSnr)) {
            const bool a = settingsStore.saveSquelchThreshold(threshold);
            const bool b = settingsStore.saveSquelchUsesSnr(useSnr);
            Serial.printf("[SETTINGS] Squelch=%u %s %s\n", threshold, useSnr ? "SNR" : "RSSI", a && b ? "saved" : "save failed");
            closeMenuToRadio("squelch selected");
          }
        } else if (menuPage == 25 && menuSelection >= 8 && menuSelection <= 12) {
          static const uint8_t attValues[] = {0, 5, 9, 17, 25};
          const uint8_t value = attValues[menuSelection - 8];
          if (radio.setAttenuationIndex(value)) {
            const bool saved = settingsStore.saveAttenuationIndex(value);
            Serial.printf("[SETTINGS] ATT index=%u %s\n", value, saved ? "saved" : "save failed");
            closeMenuToRadio("ATT selected");
          }
        } else if (menuPage == 25 && menuSelection >= 13 && menuSelection <= 15) {
          const uint8_t region = menuSelection - 13;
          if (radio.setFmRegion(region)) {
            const bool saved = settingsStore.saveFmRegion(region);
            Serial.printf("[SETTINGS] FM region=%u %s\n", region, saved ? "saved" : "save failed");
            closeMenuToRadio("FM region selected");
          }
        } else if (menuPage == 25 && menuSelection >= 16 && menuSelection <= 18) {
          const uint8_t mode = menuSelection - 16;
          if (radio.setFmStereoMode(mode)) {
            const bool saved = settingsStore.saveFmStereoMode(mode);
            Serial.printf("[SETTINGS] FM stereo=%u %s\n", mode, saved ? "saved" : "save failed");
            closeMenuToRadio("FM stereo selected");
          }
        } else if (menuPage == 11 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 5;
        } else if (menuPage == 11 && menuSelection == 1) {
          if (radio.setAgcAutomatic(true)) {
            const bool saved = settingsStore.saveAgcAutomatic(true);
            Serial.printf("[SETTINGS] AGC AUTO %s\n", saved ? "saved" : "save failed");
            closeMenuToRadio("AGC AUTO selected");
          }
        } else if (menuPage == 11 && menuSelection >= 2 && menuSelection <= 5) {
          static const uint8_t agcIndexes[] = {0, 8, 16, 24};
          const uint8_t index = agcIndexes[menuSelection - 2];
          if (radio.setAgcManualGain(index)) {
            const bool modeSaved = settingsStore.saveAgcAutomatic(false);
            const bool indexSaved = settingsStore.saveAgcGainIndex(index);
            Serial.printf("[SETTINGS] AGC manual index=%u %s\n",
              static_cast<unsigned>(index), modeSaved && indexSaved ? "saved" : "save failed");
            closeMenuToRadio("AGC gain selected");          }
        } else if (menuPage == 11 && menuSelection >= 6 && menuSelection <= 9) {
          const uint8_t attenuation = static_cast<uint8_t>((menuSelection - 6) * 4);
          if (radio.setSoftMuteAttenuation(attenuation)) {
            const bool saved = settingsStore.saveSoftMuteAttenuationDb(attenuation);
            Serial.printf("[SETTINGS] Soft Mute=%u dB %s\n",
              static_cast<unsigned>(attenuation), saved ? "saved" : "save failed");
            closeMenuToRadio("Soft Mute selected");          }
        } else if (menuPage == 11 && menuSelection >= 10 && menuSelection <= 13) {
          static const uint8_t avcGains[] = {12, 30, 60, 90};
          const uint8_t gain = avcGains[menuSelection - 10];
          if (radio.setAvcGain(gain)) {
            const bool saved = settingsStore.saveAvcGain(gain);
            Serial.printf("[SETTINGS] AVC=%u %s\n",
              static_cast<unsigned>(gain), saved ? "saved" : "save failed");
          }
        } else if (menuPage == 16 && menuSelection == 0) {
          menuPage = 0; menuSelection = 3;
        } else if (menuPage == 16 && menuSelection > 0) {
          const bool fmBand = radio.snapshot().bandMinimumKHz >= 64000;
          const RadioModulation requested = fmBand ? RadioModulation::FM :
            (menuSelection == 1 ? RadioModulation::AM :
             (menuSelection == 2 ? RadioModulation::USB : RadioModulation::LSB));
          if (radio.setModulation(requested)) {
            display.notifyTuning();
            closeMenuToRadio("modulation selected");
          }
        } else if (menuPage == 3 && menuSelection == 2) {
          menuPage = 8;
          menuSelection = static_cast<uint8_t>(radio.currentBandIndex() + 1);
        } else if (menuPage == 8 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 2;
        } else if (menuPage == 8 && menuSelection > 0) {
          if (radio.selectBand(static_cast<uint8_t>(menuSelection - 1))) {
            display.notifyTuning();
            Serial.printf("[MENU] band=%s freq=%lu kHz\n", radio.snapshot().bandName,
              static_cast<unsigned long>(radio.snapshot().frequencyKHz));
            closeMenuToRadio("band selected");
          }
        } else if (menuPage == 3 && menuSelection == 4) {
          menuPage = 9;
          menuSelection = static_cast<uint8_t>(radio.stepChoice() + 1);
        } else if (menuPage == 9 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 4;
        } else if (menuPage == 9 && menuSelection > 0) {
          if (radio.setStepChoice(static_cast<uint8_t>(menuSelection - 1))) {
            closeMenuToRadio("step size selected");
          }
        } else if (menuPage == 3 && menuSelection == 3) {
          menuPage = 7;
          menuSelection = static_cast<uint8_t>(radio.bandwidthChoice() + 1);
        } else if (menuPage == 7 && menuSelection == 0) {
          menuPage = 0;
          menuSelection = 3;
        } else if (menuPage == 7 && menuSelection > 0) {
          if (radio.setBandwidthChoice(static_cast<uint8_t>(menuSelection - 1))) {
            if (radio.snapshot().modulation == RadioModulation::LSB || radio.snapshot().modulation == RadioModulation::USB) {
              const bool saved = settingsStore.saveSsbBandwidthChoice(static_cast<uint8_t>(menuSelection - 1));
              Serial.printf("[SETTINGS] SSB bandwidth=%u %s\n", static_cast<unsigned>(menuSelection - 1), saved ? "saved" : "save failed");
            }
            Serial.printf("[MENU] bandwidth choice=%u\n", static_cast<unsigned>(menuSelection - 1));
            closeMenuToRadio("bandwidth selected");
          }
        } else if (menuPage == 2 && menuSelection == 0) {
          closeMenuToRadio("display settings finished");
        } else if (menuSelection == 0) {
          menuPage = 0;
          menuSelection = 0;
        } else if (menuPage == 1 && menuSelection == 1) {
          menuPage = 0;
          menuSelection = 1;
        }
        break;
      case EncoderEventType::LongPress:
        lastEncoderEvent = "LONG";
        if (bfoAdjustmentActive) {
          bfoAdjustmentActive = false;
          if (bfoSavePending) settingsStore.saveBfoOffsetHz(radio.bfoOffsetHz());
          bfoSavePending = false;
          Serial.println("[BFO] adjustment finished with long press");
        } else if (menuOpen) {
          if (menuPage == 22) {
            if(memoryEditActive) {
              const bool ok=memories.rename(selectedMemorySlot,memoryEditName);
              Serial.printf("[MEMORY] rename slot %u %s\n",static_cast<unsigned>(selectedMemorySlot+1),ok?"saved":"failed");
              memoryEditActive=false;
            } else if(memories.entry(selectedMemorySlot).frequencyKHz) {
              strncpy(memoryEditName,memories.entry(selectedMemorySlot).name,sizeof(memoryEditName)-1);
              memoryEditName[sizeof(memoryEditName)-1]='\0';
              if(!memoryEditName[0]) strncpy(memoryEditName,radio.snapshot().stationName[0]?radio.snapshot().stationName:radio.snapshot().bandName,sizeof(memoryEditName)-1);
              memoryEditCursor=0; memoryEditActive=true;
            } else if(memories.save(selectedMemorySlot,radio.snapshot(),radio.currentBandIndex())) Serial.printf("[MEMORY] saved slot %u\n",static_cast<unsigned>(selectedMemorySlot+1));
          }
          else if (menuPage == 23) { scanner.stop(radio); menuPage=0; menuSelection=11; }
          else if (menuPage == 24) { seeker.stop(); menuPage=0; menuSelection=10; }
          else if (menuPage == 21) { cwDecoder.exit(); menuPage=0; menuSelection=23; }
          else if (menuPage == 28) { ftxDecoder.exit(); menuPage=0; menuSelection=20; }
          else if (sliderAdjustmentActive) {
            closeMenuToRadio("slider adjustment finished with long press");
          } else if (menuPage == 6 && fmMode == 4) {
            if (fmEditName[0]) {
              char dest[96];
              snprintf(dest,sizeof(dest),"%s%s%s",fmPath,strcmp(fmPath,"/")==0?"":"/",fmEditName);
              bool ok = fmPendingAction == 5 ? FileManager::makeDirectory(dest) : FileManager::renamePath(fmSelectedPath,dest);
              fmMessageSet(ok ? "Gespeichert" : "Fehler / Ziel vorhanden");
              fmRefresh();
            } else fmMessageSet("Name fehlt");
            fmMode = 0;
          } else if (menuPage == 6 && fmMode == 0) {
            menuOpen = false;
            menuPage = 0;
            menuSelection = 0;
            fmMessageSet("Dateimanager beendet");
          } else if (menuPage == 6 && fmMode == 3) {
            fmParent();
          } else if (menuPage == 6 && fmMode != 0) {
            fmMode = 0;
            fmMessageSet("Abgebrochen");
          } else {
            saveBrightnessIfPending();
            saveThemeIfPending();
            menuOpen = false;
            menuPage = 0;
            menuSelection = 0;
            Serial.println("[MENU] closed via long press");
          }
        } else {
          Serial.println("[INPUT] long press (no action)");
        }
        break;
      case EncoderEventType::None:
      default:
        break;
    }
  }
  radio.update();
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] radio.update done");
  scanner.tick(radio);
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] scanner.tick done");
  seeker.tick(radio);
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] seeker.tick done");
  updateLastFmFrequencyPersistence();
  updateVolumePersistence();
  cwDecoder.update();
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] cwDecoder.update done");
  const RadioSnapshot& currentSnapshot = radio.snapshot();
  const bool waterfallVisible = !menuOpen && currentSnapshot.modulation != RadioModulation::FM && display.amLayout() == 4;
  performance.setWaterfallActive(waterfallVisible);
  stationLogoClient.update(currentSnapshot, wifi.connected() && wifi.dataServicesAllowed());
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] stationLogoClient.update done");
  const NewsService::Snapshot currentNews = newsService.snapshot();
  const WeatherService::Snapshot currentWeather = weatherService.snapshot();
  static String currentWifiName;
  static String currentWifiIp;
  static uint32_t lastWifiDisplayRefreshMs = 0;
  if (menuOpen && menuPage == 4 &&
      (!lastWifiDisplayRefreshMs || millis() - lastWifiDisplayRefreshMs >= 1000)) {
    currentWifiName = wifi.networkName();
    currentWifiIp = wifi.ipAddress();
    lastWifiDisplayRefreshMs = millis();
  }
  syncFrequencyCursor();
  display.setFrequencyCursor(frequencyCursorActive?static_cast<uint8_t>(frequencyCursorPos|0x80U):100);
  const MoonCalendar::Snapshot currentMoon = MoonCalendar::calculate(time(nullptr));
  if (firstLoopDiagnostics) BootDiagnostics::write("[LOOP][TRACE] before display.render");
  display.render(radio.snapshot(),radio.isReady(),wifi.connected(),lastEncoderEvent,encoderEventCount,&stationLogoClient,menuOpen,menuPage,menuSelection,radio.volume(),fmPath,fmEntries,fmCount,fmSelection,fmMode,fmActionSelection,(fmMode==4?fmEditName:fmSelectedName),fmMessage,sliderAdjustmentActive,eibi.stationName(),eibi.statusText(),eibi.available(),eibi.downloading(),&currentNews,static_cast<uint8_t>(wifi.mode()),currentWifiName.c_str(),currentWifiIp.c_str(),BootDiagnostics::enabled(),&currentWeather, menuPage == 19 ? &frequencyKeypad.view() : nullptr, &currentMoon, cwDecoder.wifiBlocked(), cwDecoder.text(), cwDecoder.signal(), selectedMemorySlot, memories.count(), scanner.count(), scanner.capacity(), static_cast<uint8_t>(scanner.state()), static_cast<uint8_t>(seeker.state()), scanner.data(), memoryEditActive, memoryEditName, memoryEditCursor, &ftxDecoder, wifi.isOff(), systemClock.synchronized());
  if (firstLoopDiagnostics) { BootDiagnostics::write("[LOOP][TRACE] display.render done; first loop complete"); firstLoopDiagnostics = false; }
  const uint32_t now=millis();
  if(now-lastStatusMs>=2000) {
    lastStatusMs=now; const RadioSnapshot& s=radio.snapshot();
    BootDiagnostics::printf("[BOOT][HEARTBEAT] uptime_ms=%lu free_heap=%u max_block=%u min_free=%u loop_stack_high_water=%u wifi=%d", static_cast<unsigned long>(now), static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<int>(WiFi.status()));
    Serial.printf("[RADIO] ready=%s freq=%.1f MHz RSSI=%d SNR=%d stereo=%s cpu=%u MHz\n",
      radio.isReady()?"yes":"no",s.frequencyKHz/1000.0f,s.rssi,s.snr,
      s.stereo?"yes":"no",static_cast<unsigned>(getCpuFrequencyMhz()));
    Serial.printf("[RDS] available=%s PI=%04X PTY=%u PS=\"%s\" RT=\"%s\"\n",
      s.rdsAvailable?"yes":"no",static_cast<unsigned>(s.piCode),
      static_cast<unsigned>(s.pty),s.stationName,s.radioText);
  }
  delay(2);
}
