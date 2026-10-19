#pragma once
#include "storage.h"
#include "config.h"
#include "events.h"

struct HistoryPoint {
  uint64_t us=0; int64_t wall=0;
  float voltage=0, current=0, dvdt=0;
  bool valid=false, currentValid=false, currentEstimated=false;
};
struct AlertHistory {
  uint32_t version=1, count=0;
  char id[48]={};
  HistoryPoint points[HISTORY_SIZE];
};
// Protected by sharedMutex. No allocation or filesystem work on enqueue.
class HistoryQueue {
 public:
  AlertHistory slots[4];
  unsigned head=0, count=0;
  bool push(const char* id,const HistoryPoint* ring,int next,int size) {
    if(count==4) return false;
    AlertHistory& s=slots[(head+count)%4]; s=AlertHistory{};
    strlcpy(s.id,id,sizeof(s.id)); s.count=size;
    for(int i=0;i<size;i++) s.points[i]=ring[(next-size+HISTORY_SIZE+i)%HISTORY_SIZE];
    count++; return true;
  }
  bool pop(AlertHistory& s) {
    if(!count) return false;
    s=slots[head]; head=(head+1)%4; count--; return true;
  }
};
inline String graphPath(const char* id) { return String("/graph-")+id+".bin"; }
inline uint32_t graphHash(const AlertHistory& s) {
  uint32_t hash=2166136261UL;
  const uint8_t* bytes=reinterpret_cast<const uint8_t*>(&s);
  for(size_t i=0;i<sizeof(s);i++) hash=(hash^bytes[i])*16777619UL;
  return hash;
}
inline bool saveGraph(Storage& storage,const AlertHistory& s) {
  String path=graphPath(s.id); File f=storage.start(path.c_str());
  uint32_t hash=graphHash(s);
  bool good=f && f.write(reinterpret_cast<const uint8_t*>(&s),sizeof(s))==sizeof(s);
  good=good && f.write(reinterpret_cast<const uint8_t*>(&hash),sizeof(hash))==sizeof(hash);
  return storage.finish(path.c_str(),f,sizeof(s)+sizeof(hash),sizeof(s)+sizeof(hash),good);
}
inline bool loadGraph(Storage& storage,const char* id,AlertHistory& s) {
  if(!storage.mounted) return false;
  File f=LittleFS.open(graphPath(id),"r"); uint32_t hash=0;
  bool good=f && f.size()==sizeof(s)+sizeof(hash) &&
    f.readBytes(reinterpret_cast<char*>(&s),sizeof(s))==sizeof(s) &&
    f.readBytes(reinterpret_cast<char*>(&hash),sizeof(hash))==sizeof(hash);
  if(f) f.close();
  return good && s.version==1 && s.count<=HISTORY_SIZE &&
    !strncmp(s.id,id,sizeof(s.id)) && graphHash(s)==hash;
}
inline void historyJson(JsonObject o,const AlertHistory& s) {
  o["status"]="available"; o["voltage_unit"]="V RMS"; o["current_unit"]="A";
  JsonArray a=o["samples"].to<JsonArray>();
  for(unsigned i=0;i<s.count;i++) {
    const HistoryPoint& p=s.points[i]; JsonObject row=a.add<JsonObject>();
    row["sample_us"]=p.us;
    if(p.wall) row["ts"]=p.wall; else row["ts"]=nullptr;
    if(p.valid) row["voltage"]=p.voltage; else row["voltage"]=nullptr;
    if(p.currentValid) row["current"]=p.current; else row["current"]=nullptr;
    row["current_estimated"]=p.currentEstimated;
  }
}

// Use the same retained event list for age expiry, capacity eviction and clear.
inline bool keepGraphFile(const String& path,const Event* events,int count) {
  for(int i=0;i<count;i++) if(path==graphPath(events[i].id)) return true;
  return false;
}
inline bool cleanGraphFile(Storage& storage,const String& path,const Event* events,int count) {
  if(keepGraphFile(path,events,count)) return true;
  return LittleFS.remove(path) || storage.fail();
}
