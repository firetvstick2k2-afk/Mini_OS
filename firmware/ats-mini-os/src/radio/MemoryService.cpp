#include "MemoryService.h"
#include <string.h>

namespace { constexpr uint8_t kEntriesPerChunk=33; }
bool MemoryService::begin() {
  ready_ = preferences_.begin("atsmini_mem", false, "settings");
  if (!ready_) return false;
  memset(&store_,0,sizeof(store_)); store_.magic=kMagic;
  for(uint8_t chunk=0;chunk<3;++chunk) {
    const uint8_t first=chunk*kEntriesPerChunk;
    const uint8_t count=(chunk==2)?(kSlotCount-first):kEntriesPerChunk;
    char key[8]; snprintf(key,sizeof(key),"s%u",static_cast<unsigned>(chunk));
    const size_t expected=count*sizeof(Entry);
    if(preferences_.getBytesLength(key)==expected)
      preferences_.getBytes(key,&store_.entries[first],expected);
  }
  for(uint8_t i=0;i<kSlotCount;++i) store_.entries[i].name[sizeof(store_.entries[i].name)-1]=0;
  Serial.printf("[MEMORY] %s; slots=%u\n",ready_?"ready":"unavailable",kSlotCount);
  return true;
}
bool MemoryService::persist() {
  if(!ready_) return false;
  for(uint8_t chunk=0;chunk<3;++chunk) {
    const uint8_t first=chunk*kEntriesPerChunk;
    const uint8_t count=(chunk==2)?(kSlotCount-first):kEntriesPerChunk;
    char key[8]; snprintf(key,sizeof(key),"s%u",static_cast<unsigned>(chunk));
    const size_t bytes=count*sizeof(Entry);
    if(preferences_.putBytes(key,&store_.entries[first],bytes)!=bytes) return false;
  }
  return true;
}
const MemoryService::Entry& MemoryService::entry(uint8_t slot) const {
  static const Entry empty{}; return slot<kSlotCount?store_.entries[slot]:empty;
}
bool MemoryService::save(uint8_t slot,const RadioSnapshot& radio,uint8_t bandIndex,const char* name) {
  if(!ready_||slot>=kSlotCount||!radio.frequencyKHz) return false;
  Entry next{}; next.frequencyKHz=radio.frequencyKHz;
  next.modulation=static_cast<uint8_t>(radio.modulation); next.bandIndex=bandIndex;
  if(name&&name[0]) strncpy(next.name,name,sizeof(next.name)-1);
  else strncpy(next.name,radio.stationName[0]?radio.stationName:radio.bandName,sizeof(next.name)-1);
  store_.entries[slot]=next; return persist();
}
bool MemoryService::erase(uint8_t slot) {
  if(!ready_||slot>=kSlotCount) return false;
  memset(&store_.entries[slot],0,sizeof(Entry)); return persist();
}
bool MemoryService::recall(uint8_t slot,RadioCore& radio) const {
  if(!ready_||slot>=kSlotCount) return false;
  const Entry& e=store_.entries[slot];
  if(!e.frequencyKHz||e.bandIndex>=radio.bandCount()) return false;
  if(!radio.selectBand(e.bandIndex)) return false;
  const RadioModulation mode=static_cast<RadioModulation>(e.modulation);
  if(radio.snapshot().modulation!=mode&&!radio.setModulation(mode)) return false;
  return radio.tune(e.frequencyKHz);
}
uint8_t MemoryService::count() const {
  uint8_t n=0; for(const auto& e:store_.entries) if(e.frequencyKHz) ++n; return n;
}
