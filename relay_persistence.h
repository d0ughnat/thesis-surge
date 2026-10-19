#pragma once
#include <ArduinoJson.h>
#include <string.h>
#include "protection.h"
struct RestoredRelay { bool lockout=false, schemaValid=true; uint8_t trips=0; };
inline RestoredRelay decodeRelay(JsonObjectConst o,bool loaded,bool damaged,bool mounted) {
  RestoredRelay r; r.lockout=damaged || !mounted;
  if(!loaded) return r;
  bool modern=(o["schema"] | 0)==2, legacy=(o["v"] | 0)==1;
  int count=o["tc"] | -1;
  bool valid=(modern || legacy) && o["tc"].is<int>() && count>=0 && count<=3 && o["lk"].is<bool>();
  int reserved=count;
  const char* state=o["state"] | "";
  if(modern) {
    bool known=!strcmp(state,"normal") || !strcmp(state,"waiting") || !strcmp(state,"checking") || !strcmp(state,"lockout");
    reserved=o["reserved_trips"] | -1;
    int expected=(o["armed"] | false) ? (count<3 ? count+1 : 3) : count;
    valid=valid && known && o["armed"].is<bool>() && o["reserved_trips"].is<int>() && reserved==expected &&
      o["lk"].as<bool>()==(!strcmp(state,"lockout"));
  } else {
    int stateNumber=o["rs"] | -1;
    valid=valid && o["rs"].is<int>() && stateNumber>=0 && stateNumber<=4;
    if(stateNumber==4) r.lockout=true;
  }
  if(!valid) { r.lockout=true; r.schemaValid=false; return r; }
  r.trips=uint8_t(reserved>count ? reserved : count);
  r.lockout=r.lockout || o["lk"].as<bool>() || r.trips>=3;
  return r;
}
inline void encodeRelay(JsonDocument& doc,const Protection& p,bool armRequested) {
  doc.clear(); doc["schema"]=2; doc["lk"]=p.state==RelayState::Lockout;
  doc["tc"]=p.tripCount; doc["state"]=relayName(p.state);
  bool armed=armRequested || p.state==RelayState::Normal;
  doc["armed"]=armed;
  doc["reserved_trips"]=armed ? (p.tripCount<3 ? int(p.tripCount)+1 : 3) : p.tripCount;
}
