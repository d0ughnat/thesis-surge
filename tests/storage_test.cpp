#include <cmath>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <ctime>
#include <fstream>
static tm* localtime_r(const time_t* value,tm* result){return localtime_s(result,value)==0?result:nullptr;}
using std::isfinite;
static size_t strlcpy(char* dst,const char* src,size_t n){size_t length=strlen(src);if(n){size_t count=length<n-1?length:n-1;memcpy(dst,src,count);dst[count]=0;}return length;}
#include "../storage.h"
#include "../events.h"
#include "../alert_history.h"
#include "../trials.h"
#include "../relay_persistence.h"
int main(){
  Storage s;s.begin();assert(s.mounted);JsonDocument doc;doc["generation"]=1;
  assert(s.commit("/state",doc,100));assert(*fake.files["/state"]=="{\"generation\":1}");
  doc["generation"]=2;fake.writeLimit=0;assert(!s.commit("/state",doc,100));assert(*fake.files["/state"]=="{\"generation\":1}");
  fake.writeLimit=SIZE_MAX;assert(!s.commit("/state",doc,1));assert(*fake.files["/state"]=="{\"generation\":1}");
  fake.failRenameAt=fake.renameCalls+2;assert(!s.commit("/state",doc,100));
  assert(!LittleFS.exists("/state") && LittleFS.exists("/state.bak"));
  bool damaged=false;JsonDocument loaded;assert(s.load("/state",loaded,100,damaged));assert(damaged && loaded["generation"]==1);
  fake.failRenameAt=0;assert(s.commit("/state",doc,100));
  *fake.files["/state"]="{broken";assert(s.load("/state",loaded,100,damaged));assert(damaged && loaded["generation"]==1);
  assert(LittleFS.exists("/state.corrupt"));assert(s.commit("/state",doc,100));
  assert(*fake.files["/state.corrupt"]=="{broken");
  fake.mount=false;Storage absent;absent.begin();assert(!absent.mounted);assert(!absent.commit("/x",doc,100));fake.mount=true;
  Event e;strlcpy(e.id,"boot-1",sizeof(e.id));strlcpy(e.boot,"boot",sizeof(e.boot));e.type=EventType::Surge;
  e.valid=true;e.rms=250;e.sampleUs=900;e.triggerUs=1000;e.commandUs=1025;e.commanded=true;
  doc.clear();eventJson(doc.to<JsonObject>(),e);assert(doc["ts"].isNull());assert(doc["response_us"]==25);assert(doc["captured_peak"].isNull());assert(doc["physical_response"]=="unknown");
  Event roundtrip;assert(readEvent(doc.as<JsonObjectConst>(),roundtrip));assert(roundtrip.commandUs==1025);
  doc["command_us"]=999;assert(!readEvent(doc.as<JsonObjectConst>(),roundtrip));
  doc["command_us"]=1025;doc["v"]="bad";assert(!readEvent(doc.as<JsonObjectConst>(),roundtrip));
  Trial t;strlcpy(t.session,"session",sizeof(t.session));t.number=1;t.complete=true;
  doc.clear();trialJson(doc.to<JsonObject>(),t);assert(doc["result"]=="no_detection");assert(doc["first_event"].isNull());assert(doc["ts"].isNull());
  t.complete=false;doc.clear();trialJson(doc.to<JsonObject>(),t);assert(doc["result"]=="incomplete");assert(doc["end_us"].isNull());
  t.complete=true;t.detections=1;t.first=e;doc.clear();trialJson(doc.to<JsonObject>(),t);Trial restored;assert(readTrial(doc.as<JsonObjectConst>(),restored));assert(restored.first.commandUs==1025);
  strlcpy(t.notes,"=1+1, quoted \"text\"\nnext line",sizeof(t.notes));
  doc.clear();trialJson(doc.to<JsonObject>(),t);std::string json;serializeJson(doc,json);
  std::ofstream("build/trial-fixture.json")<<json;
  std::ofstream csv("build/trial-fixture.csv",std::ios::binary);csv<<trialCsvHeader()<<trialCsvLine(t);csv.close();
  Protection p;p.state=RelayState::Checking;p.tripCount=2;encodeRelay(doc,p,true);
  auto boot=decodeRelay(doc.as<JsonObjectConst>(),true,false,true);assert(boot.schemaValid && boot.lockout && boot.trips==3);
  p.tripCount=0;encodeRelay(doc,p,false);boot=decodeRelay(doc.as<JsonObjectConst>(),true,false,true);assert(boot.schemaValid && !boot.lockout && boot.trips==0);
  doc.remove("armed");boot=decodeRelay(doc.as<JsonObjectConst>(),true,false,true);assert(!boot.schemaValid && boot.lockout);
  boot=decodeRelay(doc.as<JsonObjectConst>(),false,false,false);assert(boot.lockout);
  doc.clear();doc["v"]=1;doc["tc"]=1;doc["lk"]=false;doc["rs"]=4;
  boot=decodeRelay(doc.as<JsonObjectConst>(),true,false,true);assert(boot.lockout);
  fake=FakeFSState{}; Storage graphStorage;graphStorage.begin();
  HistoryPoint ring[HISTORY_SIZE];
  for(int i=0;i<HISTORY_SIZE;i++) ring[i]={uint64_t(i)*100000,0,float(230+i),1.5f,0,true,true,i%2==0};
  ring[5].valid=false;ring[5].currentValid=false;
  HistoryQueue queue;
  assert(queue.push("one",ring,0,60));
  assert(queue.push("two",ring,0,60));
  assert(queue.push("three",ring,0,60));
  assert(queue.push("four",ring,0,60));
  assert(!queue.push("overflow",ring,0,60));
  AlertHistory graph;assert(queue.pop(graph));assert(!strcmp(graph.id,"one"));assert(graph.count==60);
  assert(graph.points[59].us==5900000);assert(saveGraph(graphStorage,graph));
  Storage reboot;reboot.begin();AlertHistory restoredGraph;
  assert(loadGraph(reboot,"one",restoredGraph));assert(restoredGraph.points[59].voltage==289);
  doc.clear();historyJson(doc.to<JsonObject>(),restoredGraph);
  assert(doc["samples"][5]["voltage"].isNull());assert(doc["samples"][5]["current"].isNull());
  assert(doc["samples"][0]["current_estimated"]==true);assert(doc["samples"][0]["ts"].isNull());
  assert(!loadGraph(reboot,"legacy",restoredGraph));
  assert(queue.pop(graph));assert(!strcmp(graph.id,"two"));
  fake.writeLimit=0;assert(!saveGraph(graphStorage,graph));assert(!loadGraph(reboot,"two",restoredGraph));
  assert(loadGraph(reboot,"one",restoredGraph));fake.writeLimit=SIZE_MAX;
  assert(saveGraph(graphStorage,graph));assert(loadGraph(reboot,"two",restoredGraph));
  (*fake.files[graphPath("two")])[70]^=1;assert(!loadGraph(reboot,"two",restoredGraph));
  fake.mount=false;assert(!loadGraph(reboot,"one",restoredGraph));fake.mount=true;
  Event retained[2];strlcpy(retained[0].id,"one",sizeof(retained[0].id));strlcpy(retained[1].id,"two",sizeof(retained[1].id));
  assert(cleanGraphFile(graphStorage,graphPath("one"),retained,2));assert(LittleFS.exists(graphPath("one")));
  assert(cleanGraphFile(graphStorage,graphPath("two"),retained,1));assert(!LittleFS.exists(graphPath("two"))); // age/cap eviction
  fake.removeFails=true;assert(!cleanGraphFile(graphStorage,graphPath("one"),retained,0));
  fake.removeFails=false;assert(cleanGraphFile(graphStorage,graphPath("one"),retained,0));assert(!LittleFS.exists(graphPath("one"))); // clear
  assert(queue.push("wrapped",ring,10,60));
  assert(queue.pop(graph));assert(!strcmp(graph.id,"three"));assert(queue.pop(graph));assert(!strcmp(graph.id,"four"));
  assert(queue.pop(graph));assert(graph.points[0].us==1000000 && graph.points[59].us==900000);
  assert(!queue.pop(graph));
  puts("PASS: graph queue overflow, association, reload, missing/estimated values, short write, corruption, unavailable legacy graphs");
  puts("PASS: short writes, caps, interrupted rename, corrupt primary/backup recovery, mount failure, durable trip reservation, event timing/null quality, real no-detection and interrupted trials");
}
