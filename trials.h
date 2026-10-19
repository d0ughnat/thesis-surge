#pragma once
#include "events.h"
#include <string>
struct Trial {
  char session[48]={}, reference[96]={}, notes[160]={}, physical[96]={};
  uint16_t number=0, detections=0;
  int64_t wall=0;
  uint64_t startUs=0, endUs=0;
  uint64_t firstSampleUs=0, lastSampleUs=0, maxGapUs=0;
  uint32_t sampleCount=0, invalidSamples=0;
  bool complete=false;
  char state[16]={};
  Event first;
};
inline void trialJson(JsonObject o,const Trial& t) {
  o["session"]=t.session; o["number"]=t.number; o["reference"]=t.reference; o["notes"]=t.notes;
  o["physical_observation"]=t.physical; if(t.wall) o["ts"]=t.wall; else o["ts"]=nullptr;
  o["clock_valid"]=t.wall!=0; o["start_us"]=t.startUs; if(t.complete) o["end_us"]=t.endUs; else o["end_us"]=nullptr;
  o["complete"]=t.complete; o["detection_count"]=t.detections; o["state"]=t.state;
  o["sample_count"]=t.sampleCount; o["invalid_samples"]=t.invalidSamples; o["max_inter_burst_gap_us"]=t.maxGapUs;
  o["first_sample_us"]=t.firstSampleUs; o["last_sample_us"]=t.lastSampleUs;
  o["result"]=!t.complete ? "incomplete" : t.detections ? "detected" : "no_detection";
  if(t.detections) eventJson(o["first_event"].to<JsonObject>(),t.first); else o["first_event"]=nullptr;
  o["event_detail_policy"]="first detection retained; total count includes all detections";
}
inline bool readTrial(JsonObjectConst o,Trial& t) {
  if (!o["session"].is<const char*>() || !o["number"].is<unsigned int>()) return false;
  strlcpy(t.session,o["session"] | "",sizeof(t.session)); t.number=o["number"];
  strlcpy(t.reference,o["reference"] | "",sizeof(t.reference)); strlcpy(t.notes,o["notes"] | "",sizeof(t.notes));
  strlcpy(t.physical,o["physical_observation"] | "",sizeof(t.physical)); strlcpy(t.state,o["state"] | "unknown",sizeof(t.state));
  t.wall=o["ts"] | int64_t(0); t.startUs=o["start_us"] | uint64_t(0); t.endUs=o["end_us"] | uint64_t(0);
  t.firstSampleUs=o["first_sample_us"] | uint64_t(0); t.lastSampleUs=o["last_sample_us"] | uint64_t(0);
  t.sampleCount=o["sample_count"] | uint32_t(0); t.invalidSamples=o["invalid_samples"] | uint32_t(0);
  t.maxGapUs=o["max_inter_burst_gap_us"] | uint64_t(0);
  int count=o["detection_count"] | 0;
  if(count<0 || count>65535) return false;
  t.complete=o["complete"] | false; t.detections=uint16_t(count);
  return t.number && (!t.detections || readEvent(o["first_event"],t.first));
}
inline std::string csvQuote(std::string value) {
  if(!value.empty() && (value[0]=='=' || value[0]=='+' || value[0]=='-' || value[0]=='@' || uint8_t(value[0])<=32)) value="'"+value;
  std::string quoted="\"";
  for(char c:value) { if(c=='\"') quoted+='\"'; quoted+=c; }
  return quoted+'\"';
}
inline const char* trialCsvHeader() {
  return "session,trial,complete,result,reference,start_us,end_us,epoch,clock_valid,detection_count,event_id,type,rms_v,threshold_v,trigger_us,command_us,response_us,state,physical_observation,notes,sample_count,invalid_samples,first_sample_us,last_sample_us,max_inter_burst_gap_us\r\n";
}
inline std::string trialCsvLine(const Trial& t) {
  const Event& e=t.first; std::string row;
  auto cell=[&](std::string value) { if(!row.empty()) row+=','; row+=csvQuote(value); };
  cell(t.session); cell(std::to_string(t.number)); cell(t.complete?"true":"false");
  cell(!t.complete?"incomplete":t.detections?"detected":"no_detection"); cell(t.reference);
  cell(std::to_string(t.startUs)); cell(t.complete?std::to_string(t.endUs):""); cell(t.wall?std::to_string(t.wall):""); cell(t.wall?"true":"false");
  cell(std::to_string(t.detections)); cell(e.id); cell(t.detections?eventName(e.type):""); cell(e.valid?std::to_string(e.rms):"");
  cell(e.type>=EventType::Surge && e.type<=EventType::Undervoltage?std::to_string(eventThreshold(e.type)):"");
  cell(t.detections?std::to_string(e.triggerUs):""); cell(e.commanded?std::to_string(e.commandUs):""); cell(e.commanded?std::to_string(e.commandUs-e.triggerUs):"");
  cell(t.state); cell(t.physical); cell(t.notes); cell(std::to_string(t.sampleCount)); cell(std::to_string(t.invalidSamples));
  cell(std::to_string(t.firstSampleUs)); cell(std::to_string(t.lastSampleUs)); cell(std::to_string(t.maxGapUs));
  return row+"\r\n";
}
