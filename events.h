#pragma once
#include <ArduinoJson.h>
#include <time.h>
#include "protection.h"
struct Event {
  char id[48] = {}, boot[24] = {};
  EventType type=EventType::None;
  int64_t wall=0;
  uint64_t sampleUs=0, triggerUs=0, commandUs=0;
  float rms=0;
  bool valid=false, commanded=false, simulated=false;
};
inline float eventThreshold(EventType type) {
  return type==EventType::Surge ? SURGE_THRESHOLD : type==EventType::Overvoltage ? NORMAL_VOLTAGE_MAX_RMS :
         type==EventType::UndervoltageTrip ? UNDERVOLTAGE_TRIP_MAX_RMS : NORMAL_VOLTAGE_MIN_RMS;
}
inline void eventRecord(JsonObject o,const Event& e) {
  o["id"]=e.id; o["boot_id"]=e.boot; o["t"]=eventName(e.type);
  if(e.valid) o["v"]=e.rms; else o["v"]=nullptr;
  o["ts"]=e.wall; o["sample_us"]=e.sampleUs; o["trigger_us"]=e.triggerUs;
  o["command_us"]=e.commandUs; o["isolation_commanded"]=e.commanded; o["simulated"]=e.simulated;
}
inline void eventJson(JsonObject o, const Event& e) {
  o["id"]=e.id; o["boot_id"]=e.boot; o["t"]=eventName(e.type);
  o["protection"]=isProtection(e.type); if(e.valid) o["v"]=e.rms; else o["v"]=nullptr;
  o["simulated"]=e.simulated;
  o["measurement_type"]="filtered_RMS"; o["unit"]="V RMS";
  if ((e.type>=EventType::Surge && e.type<=EventType::Undervoltage) || e.type==EventType::UndervoltageTrip) o["threshold"]=eventThreshold(e.type);
  else o["threshold"]=nullptr;
  if(e.wall) o["ts"]=e.wall; else o["ts"]=nullptr;
  o["clock_valid"]=e.wall!=0;
  if (e.wall) {
    time_t wall=e.wall; struct tm ti; localtime_r(&wall,&ti); char date[40];
    strftime(date,sizeof(date),"%Y-%m-%dT%H:%M:%S+08:00",&ti); o["dt"]=date;
  } else o["dt"]="Time unsynchronized";
  o["sample_us"]=e.sampleUs; o["trigger_us"]=e.triggerUs;
  if (e.commanded) { o["command_us"]=e.commandUs; o["response_us"]=e.commandUs-e.triggerUs; }
  else { o["command_us"]=nullptr; o["response_us"]=nullptr; }
  o["isolation_commanded"]=e.commanded; o["physical_response"]="unknown";
  o["capture_status"]="disabled_pending_hardware_characterization";
  o["captured_peak"]=nullptr; o["duration_us"]=nullptr;
}
inline bool readEvent(JsonObjectConst o, Event& e) {
  const char* id=o["id"] | ""; const char* type=o["t"] | "";
  if (!*id || strlen(id)>=sizeof(e.id)) return false;
  e.type=EventType::None;
  for (int i=1;i<=int(EventType::UndervoltageTrip);i++) if (!strcmp(type,eventName(EventType(i)))) e.type=EventType(i);
  if (e.type==EventType::None) return false;
  strlcpy(e.id,id,sizeof(e.id)); strlcpy(e.boot,o["boot_id"] | "legacy",sizeof(e.boot));
  e.wall=o["ts"] | int64_t(0); if (e.wall<1609459200) e.wall=0;
  if(!o["v"].isNull() && !o["v"].is<float>()) return false;
  e.rms=o["v"] | 0.0f; e.valid=!o["v"].isNull() && isfinite(e.rms) && e.rms>=0 && e.rms<10000;
  if(!o["v"].isNull() && !e.valid) return false;
  e.sampleUs=o["sample_us"] | uint64_t(0); e.triggerUs=o["trigger_us"] | uint64_t(0);
  e.commandUs=o["command_us"] | uint64_t(0); e.commanded=o["isolation_commanded"] | false;
  e.simulated=o["simulated"] | false;
  return e.triggerUs>=e.sampleUs && (!e.commanded || e.commandUs>=e.triggerUs);
}
