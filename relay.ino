// RMS voltage monitoring; hardware identity/polarity and capture remain release gates.
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <time.h>
#include "config.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#include "protection.h"
#include "acquisition.h"
#include "voltage_filter.h"
#include "events.h"
#include "storage.h"
#include "alert_history.h"
#include "trials.h"
#include "relay_persistence.h"
#include "webpage.h"

WebServer server(80);
Storage storage;
Acquisition acquisition;
Protection protection;
SemaphoreHandle_t sharedMutex;
TaskHandle_t acquisitionTaskHandle=nullptr;
char bootId[24];
uint32_t eventSequence=0;
HistoryQueue graphQueue;
AlertHistory graphWork; // Loop-owned; keep large copies off task stacks.
bool graphCleanup=true;
struct Published {
  Protection p;
  Measurement measurement;
  float rms=0, current=0, dvdt=0;
  float voltageMin=0, voltageMax=0;
  bool haveVoltageRange=false;
  uint64_t sampleUs=0, heartbeatUs=0, maxGapUs=0;
  int64_t sampleWall=0;
  uint32_t revision=1, eventRevision=1, trialRevision=1;
  bool armRequested=false, armReady=false;
  Event latest;
} published;
Event eventLog[MAX_SURGE_ENTRIES];
bool relayTestActive=false, relayTestOff=false;
bool demoTripActive=false;
uint64_t demoTripUntil=0;
// Loop-owned snapshot reused by persistence and HTTP; avoids duplicate large buffers.
Event eventCopy[MAX_SURGE_ENTRIES];
int eventCount=0;
HistoryPoint history[HISTORY_SIZE];
int historyHead=0, historyCount=0;
Trial trials[MAX_TRIALS];
int trialCount=0, activeTrial=-1;
char sessionId[48]={};
uint32_t savedState=0, savedEvents=0, savedTrials=0;
uint64_t lastPersist=0, lastPrune=0;
bool taskStarted=false;

void printNetworkStatus(const char* reason) {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi %s\n", reason);
    Serial.print("Connect to dashboard: http://");
    Serial.println(WiFi.localIP());
    Serial.print("Device IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("RSSI: ");
    Serial.println(WiFi.RSSI());
  } else {
    Serial.println("WiFi offline; dashboard unavailable until reconnect.");
  }
}

void lockShared() { xSemaphoreTake(sharedMutex,portMAX_DELAY); }
void unlockShared() { xSemaphoreGive(sharedMutex); }
int64_t clockEpoch() { time_t t=time(nullptr); return t>=1609459200 ? int64_t(t) : 0; }
float demoCurrentFromVoltage(float voltage) {
  float jitter=(float(int(esp_random()%21))-10.0f)/100.0f;
  float estimated=1.5f*(voltage/NOMINAL_VOLTAGE_RMS)+jitter;
  if(voltage<NOMINAL_VOLTAGE_RMS) return constrain(estimated,1.0f,1.49f);
  return constrain(estimated,1.5f,2.5f);
}
int todayProtectionCount() {
  int64_t now=clockEpoch(); if(!now) return -1;
  int64_t day=(now+GMT_OFFSET_S)/86400; int count=0;
  lockShared(); for(int i=0;i<eventCount;i++)
    if(eventLog[i].wall && (eventLog[i].wall+GMT_OFFSET_S)/86400==day && isProtection(eventLog[i].type)) count++;
  unlockShared(); return count;
}
void commandLoad(bool on) {
  // Relay module is active-low: GPIO26 LOW commands ON, HIGH commands OFF.
  digitalWrite(RELAY_PIN, on ? LOAD_ON_LEVEL : LOAD_OFF_LEVEL);
}
void updateLeds(const Protection& p) {
  bool red=p.state==RelayState::Lockout || (p.valid && p.surgeActive && p.voltage>=SURGE_THRESHOLD);
  bool green=!red && p.valid && p.voltage>=NORMAL_VOLTAGE_MIN_RMS && p.voltage<=NORMAL_VOLTAGE_MAX_RMS;
  bool amber=!red && !green;
  digitalWrite(LED_GREEN_PIN, LED_ACTIVE_HIGH ? green : !green);
  digitalWrite(LED_AMBER_PIN, LED_ACTIVE_HIGH ? amber : !amber);
  digitalWrite(LED_RED_PIN, LED_ACTIVE_HIGH ? red : !red);
}
// Called with sharedMutex after the command. Bounded RAM operations only.
void publishEvent(const Event& e) {
  if(eventCount==MAX_SURGE_ENTRIES) {
    memmove(eventLog,eventLog+1,(MAX_SURGE_ENTRIES-1)*sizeof(Event)); eventCount--; graphCleanup=true;
  }
  eventLog[eventCount++]=e; published.eventRevision++;
  if(isProtection(e.type)) {
    graphQueue.push(e.id,history,historyHead,historyCount);
    published.latest=e;
    if(activeTrial>=0) {
      Trial& t=trials[activeTrial];
      if(!t.detections) t.first=e;
      if(t.detections<65535) t.detections++;
      published.trialRevision++;
    }
  }
}
Event makeEvent(EventType type,uint64_t trigger,uint64_t command,bool commanded,const Measurement& m,float rms) {
  Event e;
  lockShared(); uint32_t sequence=++eventSequence; unlockShared();
  snprintf(e.id,sizeof(e.id),"%s-%08lx",bootId,(unsigned long)sequence);
  strlcpy(e.boot,bootId,sizeof(e.boot)); e.type=type; e.wall=clockEpoch();
  e.sampleUs=m.endUs; e.triggerUs=trigger; e.commandUs=command; e.commanded=commanded;
  e.rms=rms; e.valid=m.valid; return e;
}
void acquisitionOwner(void*) {
  ResetButton reset;
  uint64_t next=0, previousSample=0;
  float filtered=0, previousVoltage=0; bool previousValid=false;
  VoltageFilter voltageFilter;
  for(;;) {
    uint64_t now=esp_timer_get_time();
    if(reset.update(digitalRead(RESET_PIN)==HIGH,now) && protection.state==RelayState::Lockout) {
      protection.reset(now); commandLoad(false); updateLeds(protection);
      lockShared(); published.p=protection; published.revision++; unlockShared();
    }
    if(now>=next) {
      Measurement m=acquisition.read();
      voltageFilter.update(m.valid,m.rawRms*CAL_DEFAULT);
      // Show each valid RMS burst without hiding isolated changes in the UI.
      // Protection retains its median filter and confirmation requirement.
      filtered=m.valid ? m.rawRms*CAL_DEFAULT : 0;
      float dvdt=previousValid && m.valid && previousSample ? (filtered-previousVoltage)*1000000.0f/(m.endUs-previousSample) : 0;
      previousSample=m.endUs; previousVoltage=filtered; previousValid=m.valid;
      // Current is only meaningful while the voltage measurement is valid.
      float currentAmps=m.currentRawRms*CURRENT_CAL_A_PER_V;
      if(currentAmps<CURRENT_NOISE_FLOOR_A) currentAmps=0;
      bool currentAvailable=m.valid && filtered>1.0f && m.currentValid;
      bool currentEstimated=m.valid && filtered>1.0f && !currentAvailable;
      if(currentEstimated) currentAmps=demoCurrentFromVoltage(filtered);
      m.currentValid=currentAvailable;
      lockShared();
      bool testActive=relayTestActive, testOff=relayTestOff, demoActive=demoTripActive;
      uint64_t demoUntil=demoTripUntil;
      if(demoActive && now>=demoUntil) { demoTripActive=false; demoTripUntil=0; demoActive=false; }
      unlockShared();
      bool keepRelayOnForSensors=RELAY_POWERS_SENSORS && !testActive && protection.state!=RelayState::Lockout;
      RelayState before=protection.state; uint8_t beforeCount=protection.tripCount;
      uint64_t trigger=esp_timer_get_time();
      Decision decision=protection.step(m.valid,voltageFilter.rms,trigger);
      uint64_t command=0;
      // No logging, locking, JSON, Serial or filesystem before the OFF command.
      if(decision.commandOff && !testActive && !demoActive && !keepRelayOnForSensors) { commandLoad(false); command=esp_timer_get_time(); }
      // Reserve a possible next trip durably before energizing. Power loss before a
      // deferred OFF-state commit then restores the conservative reserved count.
      lockShared();
      if(decision.commandOff && !testActive && !demoActive && !keepRelayOnForSensors) { published.armRequested=false; published.armReady=false; }
      if(decision.commandOn && !testActive && !published.armReady) {
        protection.state=RelayState::Checking; decision.commandOn=false;
        if(!published.armRequested) { published.armRequested=true; published.revision++; }
      }
      unlockShared();
      if(demoActive) commandLoad(false);
      else if(testActive) commandLoad(!testOff);
      else if(demoUntil) commandLoad(true);
      else if(keepRelayOnForSensors) commandLoad(true);
      else if(decision.commandOn) commandLoad(true);
      updateLeds(protection);
      Event e;
      if(decision.event!=EventType::None)
        e=makeEvent(decision.event,trigger,command,decision.commandOff && isProtection(decision.event),m,voltageFilter.rms);
      int64_t wall=clockEpoch();
      lockShared();
      published.p=protection; published.measurement=m; published.rms=filtered; published.dvdt=dvdt;
      if(m.valid) {
        if(!published.haveVoltageRange) {
          published.voltageMin=published.voltageMax=filtered;
          published.haveVoltageRange=true;
        } else {
          if(filtered<published.voltageMin) published.voltageMin=filtered;
          if(filtered>published.voltageMax) published.voltageMax=filtered;
        }
      }
      published.current=(currentAvailable || currentEstimated) ? currentAmps : 0;
      published.sampleUs=m.endUs; published.sampleWall=wall;
      if(m.gapUs>published.maxGapUs) published.maxGapUs=m.gapUs;
      if(before!=protection.state || beforeCount!=protection.tripCount) published.revision++;
      history[historyHead]={m.endUs,wall,filtered,published.current,dvdt,m.valid,currentAvailable || currentEstimated,currentEstimated};
      historyHead=(historyHead+1)%HISTORY_SIZE; if(historyCount<HISTORY_SIZE) historyCount++;
      if(activeTrial>=0) {
        Trial& t=trials[activeTrial];
        if(!t.sampleCount) t.firstSampleUs=m.endUs;
        t.sampleCount++; if(!m.valid) t.invalidSamples++;
        t.lastSampleUs=m.endUs; if(m.gapUs>t.maxGapUs) t.maxGapUs=m.gapUs;
      }
      if(isProtection(e.type)) publishEvent(e);
      unlockShared();
      next=now+VOLTAGE_SAMPLE_PERIOD_US;
    }
    lockShared(); published.heartbeatUs=esp_timer_get_time(); unlockShared();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void loadRecords() {
  JsonDocument doc; bool damaged=false;
  bool loaded=storage.load(RELAY_PATH,doc,2048,damaged);
  RestoredRelay restored=decodeRelay(doc.as<JsonObjectConst>(),loaded,damaged,storage.mounted);
  if(!restored.schemaValid) { storage.fail(); storage.quarantine(RELAY_PATH); }
  protection.restore(restored.lockout,restored.trips,esp_timer_get_time()); published.p=protection;
  doc.clear();
  if(storage.load(SURGE_PATH,doc,EVENT_FILE_BYTE_CAP,damaged)) {
    JsonArray array=doc["surges"].as<JsonArray>();
    if(array.isNull() || array.size()>MAX_SURGE_ENTRIES) { storage.fail(); storage.quarantine(SURGE_PATH); }
    else for(JsonObject o:array) {
      Event e;
      if(o["id"].isNull()) {
        char id[48]; snprintf(id,sizeof(id),"legacy-%lld-%d",(long long)(o["ts"] | int64_t(0)),eventCount);
        o["id"]=id; o["boot_id"]="legacy";
        if(o["t"].isNull()) o["t"]="SURGE";
      }
      if(readEvent(o,e)) {
        if(isProtection(e.type)) { eventLog[eventCount++]=e; published.latest=e; }
      } else { storage.fail(); storage.quarantine(SURGE_PATH); }
    }
  }
  doc.clear();
  if(storage.load("/trials.json",doc,TRIAL_FILE_BYTE_CAP,damaged)) {
    JsonArray array=doc["trials"].as<JsonArray>();
    if(array.isNull() || array.size()>MAX_TRIALS) { storage.fail(); storage.quarantine("/trials.json"); }
    else for(JsonObject o:array) { Trial t; if(readTrial(o,t)) trials[trialCount++]=t; else { storage.fail(); storage.quarantine("/trials.json"); } }
  }
}
void persistRecords() {
  uint64_t now=esp_timer_get_time();
  if(now-lastPersist<5000000ULL) return; lastPersist=now;
  Published snap; lockShared(); snap=published; unlockShared();
  JsonDocument doc;
  if(savedState!=snap.revision) {
    encodeRelay(doc,snap.p,snap.armRequested);
    if(storage.commit(RELAY_PATH,doc,2048)) {
      savedState=snap.revision;
      lockShared(); if(published.revision==snap.revision && published.armRequested) published.armReady=true; unlockShared();
    }
    else return;
  }
  if(savedEvents!=snap.eventRevision) {
    int count;
    lockShared(); count=eventCount; memcpy(eventCopy,eventLog,count*sizeof(Event)); uint32_t rev=published.eventRevision; unlockShared();
    File f=storage.start(SURGE_PATH); const char* prefix="{\"schema\":2,\"surges\":[";
    size_t expected=strlen(prefix); bool good=f && f.print(prefix)==expected;
    for(int i=0;i<count && good;i++) {
      if(i) { good=f.print(',')==1; expected++; }
      doc.clear(); eventRecord(doc.to<JsonObject>(),eventCopy[i]); size_t n=measureJson(doc); expected+=n;
      good=good && expected+2<=EVENT_FILE_BYTE_CAP && !doc.overflowed() && serializeJson(doc,f)==n;
    }
    expected+=2; good=good && f.print("]}")==2;
    if(storage.finish(SURGE_PATH,f,expected,EVENT_FILE_BYTE_CAP,good)) savedEvents=rev;
  }
  if(savedTrials!=snap.trialRevision) {
    doc.clear();
    int count; uint32_t rev;
    lockShared(); count=trialCount; rev=published.trialRevision; unlockShared();
    File f=storage.start("/trials.json"); const char* prefix="{\"schema\":1,\"trials\":["; size_t expected=strlen(prefix); bool good=f && f.print(prefix)==expected;
    for(int i=0;i<count && good;i++) {
      Trial t; lockShared(); t=trials[i]; unlockShared();
      if(i) { good=f.print(',')==1; expected++; }
      doc.clear(); trialJson(doc.to<JsonObject>(),t); size_t n=measureJson(doc); expected+=n;
      good=good && expected+2<=TRIAL_FILE_BYTE_CAP && !doc.overflowed() && serializeJson(doc,f)==n;
    }
    expected+=2; good=good && f.print("]}")==2;
    if(storage.finish("/trials.json",f,expected,TRIAL_FILE_BYTE_CAP,good)) savedTrials=rev;
  }
}
void pruneRecords() {
  uint64_t now=esp_timer_get_time(); if(now-lastPrune<60000000ULL) return; lastPrune=now;
  int64_t wall=clockEpoch(); if(!wall) return;
  lockShared(); int count=0;
  for(int i=0;i<eventCount;i++) if(!eventLog[i].wall || eventLog[i].wall>=wall-SURGE_RETAIN_DAYS*86400LL) eventLog[count++]=eventLog[i];
  if(count!=eventCount) { eventCount=count; published.eventRevision++; graphCleanup=true; }
  unlockShared();
}
// Filesystem work only on loop(), after acquisition has issued relay commands.
void serviceGraphs() {
  lockShared(); bool have=graphQueue.pop(graphWork); bool retained=false;
  if(have) for(int i=0;i<eventCount;i++) if(!strcmp(eventLog[i].id,graphWork.id)) retained=true;
  bool cleanup=graphCleanup; graphCleanup=false; unlockShared();
  if(have && retained) saveGraph(storage,graphWork);
  if(!cleanup || !storage.mounted) return;
  int count; lockShared(); count=eventCount; memcpy(eventCopy,eventLog,count*sizeof(Event)); unlockShared();
  File root=LittleFS.open("/");
  if(!root) { storage.fail(); lockShared(); graphCleanup=true; unlockShared(); return; }
  for(File f=root.openNextFile();f;f=root.openNextFile()) {
    String path=f.name(); f.close(); if(!path.startsWith("/")) path="/"+path;
    if(!path.startsWith("/graph-")) continue;
    if(!cleanGraphFile(storage,path,eventCopy,count)) { lockShared(); graphCleanup=true; unlockShared(); }
  }
  root.close();
}
void sendJson(JsonDocument& doc,int code=200) {
  if(doc.overflowed()) { server.send(503,"application/json","{\"error\":\"allocation_failed\"}"); return; }
  String body; serializeJson(doc,body); server.sendHeader("Cache-Control","no-store"); server.send(code,"application/json",body);
}
void handleData() {
  Published p; lockShared(); p=published; unlockShared();
  JsonDocument doc; uint64_t now=esp_timer_get_time();
  bool displayVoltage=p.measurement.valid && p.sampleUs && p.rms>=DISPLAY_VOLTAGE_MIN_RMS;
  if(displayVoltage) doc["voltage"]=p.rms; else doc["voltage"]=nullptr;
  if(p.haveVoltageRange) {
    doc["voltage_min_since_boot"]=p.voltageMin;
    doc["voltage_max_since_boot"]=p.voltageMax;
  } else {
    doc["voltage_min_since_boot"]=nullptr;
    doc["voltage_max_since_boot"]=nullptr;
  }
  doc["voltage_sample_period_us"]=VOLTAGE_SAMPLE_PERIOD_US;
  bool currentEstimated=p.measurement.valid && !p.measurement.currentValid && p.rms>1.0f && p.current>0;
  if(displayVoltage && (p.measurement.currentValid || currentEstimated)) doc["current"]=p.current; else doc["current"]=nullptr;
  if(displayVoltage && (p.measurement.currentValid || currentEstimated)) doc["power"]=p.rms*p.current; else doc["power"]=nullptr;
  doc["current_estimated"]=currentEstimated;
  doc["current_demo_mode"]=true;
  doc["current_sensor_pin"]=CURRENT_PIN; doc["current_cal_a_per_v"]=CURRENT_CAL_A_PER_V;
  doc["current_sensor_ok"]=p.measurement.currentValid; doc["current_adc_mean"]=p.measurement.currentMean;
  doc["current_adc_min"]=p.measurement.currentMinimum; doc["current_adc_max"]=p.measurement.currentMaximum;
  doc["dvdt"]=p.dvdt;
  if(!p.measurement.valid) doc["dvdt"]=nullptr;
  doc["status"]=p.p.classification(); doc["nominal"]=NOMINAL_VOLTAGE_RMS;
  doc["vmin"]=NORMAL_VOLTAGE_MIN_RMS; doc["vmax"]=NORMAL_VOLTAGE_MAX_RMS;
  doc["timestamp_us"]=p.sampleUs; // Compatibility alias now explicitly describes acquisition.
  int today=todayProtectionCount(); if(today>=0) doc["todaySurges"]=today; else doc["todaySurges"]=nullptr;
  doc["wifi_rssi"]=WiFi.status()==WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["sensorOk"]=p.measurement.valid; doc["systemState"]=p.p.systemState(); doc["fault_reason"]=p.p.reason();
  doc["voltageClass"]=p.p.classification(); doc["relayState"]=relayName(p.p.state);
  lockShared();
  doc["relay_test_off"]=relayTestOff; doc["relay_test_active"]=relayTestActive; doc["relay_test_on"]=relayTestActive && !relayTestOff;
  doc["demo_trip_active"]=demoTripActive;
  if(demoTripActive && demoTripUntil>now) doc["demo_trip_remaining_ms"]=(demoTripUntil-now)/1000; else doc["demo_trip_remaining_ms"]=0;
  unlockShared();
  doc["relay_powers_sensors"]=RELAY_POWERS_SENSORS;
  doc["sample_us"]=p.sampleUs; if(p.sampleUs) doc["sample_age_ms"]=(now-p.sampleUs)/1000; else doc["sample_age_ms"]=nullptr;
  if(p.sampleWall) doc["sample_epoch"]=p.sampleWall; else doc["sample_epoch"]=nullptr;
  doc["sample_clock_valid"]=p.sampleWall!=0; doc["ntp"]=clockEpoch()!=0;
  doc["boot_id"]=bootId; doc["uptime"]=now/1000000;
  doc["storage_ok"]=storage.healthy; doc["storage_failures"]=storage.failures;
  doc["reconnect_storage_pending"]=p.armRequested && !p.armReady;
  doc["heap_free"]=ESP.getFreeHeap(); doc["heap_min"]=ESP.getMinFreeHeap(); doc["heap_largest"]=ESP.getMaxAllocHeap();
  doc["task_started"]=taskStarted; doc["task_heartbeat_age_ms"]=(now-p.heartbeatUs)/1000;
  doc["task_stack_high_water"]=acquisitionTaskHandle ? uxTaskGetStackHighWaterMark(acquisitionTaskHandle) : 0;
  doc["reset_reason"]=int(esp_reset_reason());
  doc["burst_duration_us"]=p.measurement.endUs-p.measurement.startUs;
  doc["inter_burst_gap_us"]=p.measurement.gapUs; doc["max_inter_burst_gap_us"]=p.maxGapUs;
  doc["max_sample_interval_us"]=p.measurement.maxIntervalUs; doc["adc_samples"]=ADC_SAMPLES;
  doc["raw_rms_adc_volts"]=p.measurement.rawRms; doc["adc_min"]=p.measurement.minimum; doc["adc_max"]=p.measurement.maximum;
  doc["voltage_sensor_reason"]=p.measurement.invalidReason();
  doc["voltage_adc_mean"]=p.measurement.mean;
  doc["voltage_adc_mean_drift"]=p.measurement.meanDrift;
  doc["voltage_zero_crossings"]=p.measurement.crossings;
  doc["voltage_cal_default"]=CAL_DEFAULT;
  if(p.measurement.valid && p.measurement.rawRms>0.0f) doc["voltage_scale_applied"]=p.rms/p.measurement.rawRms;
  else doc["voltage_scale_applied"]=nullptr;
  doc["capture_enabled"]=false; doc["capture_status"]="disabled_pending_hardware_characterization";
  if(p.latest.id[0]) eventJson(doc["latest_event"].to<JsonObject>(),p.latest); else doc["latest_event"]=nullptr;
  sendJson(doc);
}
void handleHistory() {
  HistoryPoint copy[HISTORY_SIZE]; int count;
  lockShared(); count=historyCount; int start=(historyHead-historyCount+HISTORY_SIZE)%HISTORY_SIZE;
  for(int i=0;i<count;i++) copy[i]=history[(start+i)%HISTORY_SIZE]; unlockShared();
  JsonDocument doc; doc["boot_id"]=bootId; JsonArray a=doc["samples"].to<JsonArray>();
  JsonArray voltages=doc["voltage"].to<JsonArray>(), currents=doc["current"].to<JsonArray>(), derivatives=doc["dvdt"].to<JsonArray>();
  for(int i=0;i<count;i++) {
    JsonObject o=a.add<JsonObject>(); o["sample_us"]=copy[i].us; o["valid"]=copy[i].valid;
    if(copy[i].wall) o["ts"]=copy[i].wall; else o["ts"]=nullptr;
    bool displayVoltage=copy[i].valid && copy[i].voltage>=DISPLAY_VOLTAGE_MIN_RMS;
    if(displayVoltage) o["voltage"]=copy[i].voltage; else o["voltage"]=nullptr;
    if(displayVoltage && copy[i].currentValid) o["current"]=copy[i].current; else o["current"]=nullptr;
    o["current_estimated"]=copy[i].currentEstimated;
    o["dvdt"]=copy[i].dvdt;
    if(displayVoltage) { voltages.add(copy[i].voltage); derivatives.add(copy[i].dvdt); }
    else { voltages.add(nullptr); derivatives.add(nullptr); o["dvdt"]=nullptr; }
    if(displayVoltage && copy[i].currentValid) currents.add(copy[i].current); else currents.add(nullptr);
  }
  sendJson(doc);
}
void handleSurges() {
  int count;
  lockShared(); count=eventCount; memcpy(eventCopy,eventLog,count*sizeof(Event)); unlockShared();
  JsonDocument doc; doc["count"]=count; doc["boot_id"]=bootId;
  int today=todayProtectionCount(); if(today>=0) doc["todayCount"]=today; else doc["todayCount"]=nullptr;
  String since=server.arg("since"); bool found=since.isEmpty(); int after=-1;
  for(int i=0;i<count;i++) if(since==eventCopy[i].id) { found=true; after=i; }
  doc["retention_gap"]=!found; doc["oldest_id"]=count ? eventCopy[0].id : ""; doc["newest_id"]=count ? eventCopy[count-1].id : "";
  String prefix; serializeJson(doc,prefix); prefix.remove(prefix.length()-1); prefix+=",\"entries\":[";
  server.sendHeader("Cache-Control","no-store"); server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200,"application/json",""); server.sendContent(prefix);
  if(server.arg("all")=="1") after=-1;
  for(int i=count-1;i>after;i--) {
    doc.clear(); eventJson(doc.to<JsonObject>(),eventCopy[i]); String body; serializeJson(doc,body);
    if(i<count-1) server.sendContent(","); server.sendContent(body);
  }
  server.sendContent("]}"); server.sendContent("");
}
void handleEvent() {
  String id=server.arg("id"); Event e; bool found=false;
  lockShared(); for(int i=0;i<eventCount;i++) if(id==eventLog[i].id) { e=eventLog[i]; found=true; break; }
  unlockShared();
  JsonDocument doc;
  if(found) {
    eventJson(doc.to<JsonObject>(),e); doc["samples"].to<JsonArray>();
    JsonObject h=doc["history"].to<JsonObject>();
    bool pending=false; lockShared();
    for(unsigned i=0;i<graphQueue.count;i++) if(id==graphQueue.slots[(graphQueue.head+i)%4].id) pending=true;
    unlockShared();
    if(loadGraph(storage,e.id,graphWork)) historyJson(h,graphWork);
    else { h["status"]=pending ? "pending" : "unavailable"; h["samples"].to<JsonArray>(); }
  }
  else doc["error"]="event_not_retained";
  sendJson(doc,found ? 200 : 404);
}
void handleClearLogs() {
  if(!strlen(CLEAR_TOKEN) || !server.hasArg("token") || server.arg("token")!=CLEAR_TOKEN) {
    server.send(403,"application/json","{\"error\":\"disabled_or_bad_token\"}"); return;
  }
  uint32_t rev; lockShared(); rev=published.eventRevision; unlockShared();
  JsonDocument doc; doc["schema"]=2; doc["surges"].to<JsonArray>();
  bool stored=storage.commit(SURGE_PATH,doc,EVENT_FILE_BYTE_CAP);
  lockShared();
  if(published.eventRevision==rev) { eventCount=0; published.eventRevision++; savedEvents=stored ? published.eventRevision : 0; graphQueue.count=0; graphCleanup=true; }
  else { savedEvents=0; unlockShared(); server.send(409,"application/json","{\"error\":\"new_event_arrived_retry\"}"); return; }
  published.latest=Event{};
  unlockShared();
  if(stored) server.send(200,"application/json","{\"ok\":true}");
  else server.send(200,"application/json","{\"ok\":true,\"warning\":\"storage_write_failed_ram_cleared\"}");
}
void handleRelayTest() {
  bool off=server.hasArg("off") && server.arg("off")=="1";
  lockShared(); relayTestActive=!off; relayTestOff=false; unlockShared();
  if(off) commandLoad(RELAY_POWERS_SENSORS);
  else commandLoad(true); // Explicit relay test may energize even while sensing is invalid.
  server.send(200,"application/json",off ? "{\"ok\":true,\"relay_test_active\":false,\"relay_test_on\":false}" : "{\"ok\":true,\"relay_test_active\":true,\"relay_test_on\":true}");
}
void handleSimulateSurge() {
  uint64_t trigger=esp_timer_get_time();
  commandLoad(false);
  uint64_t command=esp_timer_get_time();
  Published p; lockShared(); p=published; relayTestActive=false; relayTestOff=false; demoTripActive=true; demoTripUntil=trigger+DEMO_TRIP_DURATION_MS*1000ULL; unlockShared();
  Event e=makeEvent(EventType::Surge,trigger,command,true,p.measurement,p.rms);
  lockShared();
  publishEvent(e);
  published.latest=e;
  unlockShared();
  server.send(200,"application/json","{\"ok\":true,\"demo_trip_active\":true}");
}
void handleTrials() {
  int count; lockShared(); count=trialCount; unlockShared();
  JsonDocument doc;
  server.sendHeader("Content-Disposition","attachment; filename=trials.json"); server.sendHeader("Cache-Control","no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200,"application/json","");
  server.sendContent("{\"capacity\":"+String(MAX_TRIALS)+",\"count\":"+String(count)+",\"trials\":[");
  for(int i=0;i<count;i++) {
    Trial t; lockShared(); t=trials[i]; unlockShared(); doc.clear(); trialJson(doc.to<JsonObject>(),t);
    String body; serializeJson(doc,body); if(i) server.sendContent(","); server.sendContent(body);
  }
  server.sendContent("]}"); server.sendContent("");
}
void handleTrialsCsv() {
  server.sendHeader("Content-Disposition","attachment; filename=trials.csv"); server.sendHeader("Cache-Control","no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); server.send(200,"text/csv; charset=utf-8","");
  server.sendContent(trialCsvHeader());
  int count; lockShared(); count=trialCount; unlockShared();
  for(int i=0;i<count;i++) {
    Trial t; lockShared(); t=trials[i]; unlockShared();
    std::string row=trialCsvLine(t); server.sendContent(row.c_str());
  }
  server.sendContent("");
}
void serialCommand(const char* line) {
  JsonDocument doc;
  if(deserializeJson(doc,line)) { Serial.println("ERR expected JSON command"); return; }
  String cmd=doc["cmd"] | "";
  if(cmd=="session") {
    const char* id=doc["id"] | "";
    if(activeTrial>=0 || !*id || strlen(id)>=sizeof(sessionId)) { Serial.println("ERR session requires idle trial and ID 1..47 chars"); return; }
    strlcpy(sessionId,id,sizeof(sessionId)); Serial.println("OK session selected; existing trials retained");
  } else if(cmd=="start") {
    int number=doc["number"] | 0; const char* reference=doc["reference"] | "";
    if(!*sessionId || number<1 || number>65535 || !*reference || strlen(reference)>=96) { Serial.println("ERR session, number and reference required"); return; }
    lockShared(); bool duplicate=false;
    for(int i=0;i<trialCount;i++) if(!strcmp(trials[i].session,sessionId) && trials[i].number==number) duplicate=true;
    if(activeTrial>=0 || trialCount>=MAX_TRIALS || duplicate) { unlockShared(); Serial.println("ERR active, full or duplicate trial"); return; }
    activeTrial=trialCount++; Trial& t=trials[activeTrial]; t=Trial{};
    strlcpy(t.session,sessionId,sizeof(t.session)); strlcpy(t.reference,reference,sizeof(t.reference));
    t.number=number; t.startUs=esp_timer_get_time(); t.wall=clockEpoch(); published.trialRevision++;
    unlockShared(); Serial.println("OK trial started");
  } else if(cmd=="end") {
    const char* notes=doc["notes"] | ""; const char* physical=doc["physical"] | "not measured";
    if(strlen(notes)>=160 || strlen(physical)>=96) { Serial.println("ERR notes max159, physical max95 chars"); return; }
    lockShared();
    if(activeTrial<0) { unlockShared(); Serial.println("ERR no active trial"); return; }
    Trial& t=trials[activeTrial]; t.endUs=esp_timer_get_time(); t.complete=true;
    strlcpy(t.notes,notes,sizeof(t.notes)); strlcpy(t.physical,physical,sizeof(t.physical));
    strlcpy(t.state,relayName(published.p.state),sizeof(t.state)); activeTrial=-1; published.trialRevision++;
    unlockShared(); Serial.println("OK trial ended; no detection is retained as a result");
  } else if(cmd=="calibration") {
    Published p; lockShared(); p=published; unlockShared();
    float reference=doc["reference_v"] | 0.0f;
    if(!p.measurement.valid || p.measurement.rawRms<=0.0f || reference<=0 || reference>300 || !p.sampleUs || esp_timer_get_time()-p.sampleUs>2500000ULL) {
      Serial.println("ERR valid recent sample and measured reference_v in (0,300] required"); return;
    }
    Serial.printf("Calibration reference=%.3f V, raw_rms_adc_volts=%.6f, current CAL_DEFAULT=%.6f, displayed=%.3f V\n",
      reference,p.measurement.rawRms,CAL_DEFAULT,p.rms);
    Serial.printf("Proposed CAL_DEFAULT=%.6f; verify against independent reference before editing config.h. Not applied.\n",
      reference/p.measurement.rawRms);
  } else if(cmd=="clear_trials") {
    if(activeTrial>=0 || strcmp(doc["confirmation"] | "","EXPORTED")) { Serial.println("ERR idle session and confirmation EXPORTED required"); return; }
    JsonDocument empty; empty["schema"]=1; empty["trials"].to<JsonArray>();
    if(!storage.commit("/trials.json",empty,TRIAL_FILE_BYTE_CAP)) { Serial.println("ERR trial storage write failed; records retained in RAM"); return; }
    lockShared(); trialCount=0; published.trialRevision++; savedTrials=published.trialRevision; unlockShared();
    Serial.println("OK trial records cleared; protection and event history retained");
  } else Serial.println("ERR commands: session, start, end, calibration, clear_trials");
}
void serviceSerial() {
  static char line[512]; static size_t used=0; static bool overflow=false;
  for(int n=0;n<64 && Serial.available();n++) {
    char c=Serial.read();
    if(c=='\n') { if(!overflow && used) { line[used]=0; serialCommand(line); } else if(overflow) Serial.println("ERR command too long"); used=0; overflow=false; }
    else if(c!='\r') { if(used<sizeof(line)-1) line[used++]=c; else overflow=true; }
  }
}
void setup() {
  digitalWrite(RELAY_PIN,LOAD_OFF_LEVEL); pinMode(RELAY_PIN,OUTPUT); commandLoad(false);
  pinMode(RESET_PIN,INPUT_PULLUP);
  pinMode(LED_GREEN_PIN,OUTPUT); pinMode(LED_AMBER_PIN,OUTPUT); pinMode(LED_RED_PIN,OUTPUT); updateLeds(protection);
  Serial.begin(115200); analogReadResolution(12); analogSetAttenuation(ADC_11db);
  sharedMutex=xSemaphoreCreateMutex();
  if(!sharedMutex) { Serial.println("FAULT mutex allocation failed; OFF retained"); return; }
  snprintf(bootId,sizeof(bootId),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
  storage.begin(); loadRecords(); updateLeds(protection);
  taskStarted=xTaskCreate(acquisitionOwner,"acquisition",6144,nullptr,2,&acquisitionTaskHandle)==pdPASS;
  if(!taskStarted) { protection.restore(true,3,esp_timer_get_time()); published.p=protection; updateLeds(protection); }
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  uint32_t wifiStart=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-wifiStart<15000UL) {
    delay(250); Serial.print(".");
  }
  Serial.println();
  printNetworkStatus(WiFi.status()==WL_CONNECTED ? "connected" : "connection pending");
  configTime(GMT_OFFSET_S,DST_OFFSET_S,NTP_SERVER);
  server.on("/",HTTP_GET,[](){ server.sendHeader("Cache-Control","no-store"); server.send_P(200,"text/html; charset=utf-8",PAGE_HTML); });
  server.on("/data",HTTP_GET,handleData); server.on("/history",HTTP_GET,handleHistory);
  server.on("/surges",HTTP_GET,handleSurges); server.on("/events",HTTP_GET,handleEvent);
  server.on("/trials",HTTP_GET,handleTrials); server.on("/trials.csv",HTTP_GET,handleTrialsCsv);
  server.on("/clearlogs",HTTP_POST,handleClearLogs);
  server.on("/relay-test",HTTP_POST,handleRelayTest);
  server.on("/simulate-surge",HTTP_POST,handleSimulateSurge);
  server.on("/clearlogs",HTTP_GET,[](){server.send(405,"application/json","{\"error\":\"POST_required\"}");});
  server.on("/favicon.ico",HTTP_GET,[](){server.send(204);}); server.begin();
  Serial.println("Monitoring started. Capture disabled pending measured hardware profile. Serial accepts JSON trial commands.");
  Serial.printf("Voltage sensor: GPIO%d | Current sensor: ACS712 on GPIO%d | Sensitivity: %.0f mV/A | Scale: %.4f A/V\n", VOLTAGE_PIN, CURRENT_PIN, ACS712_SENSITIVITY_MV_PER_A, CURRENT_CAL_A_PER_V);
  Serial.println("Test commands (newline terminated):");
  Serial.println("  {\"cmd\":\"session\",\"id\":\"bench-001\"}");
  Serial.println("  {\"cmd\":\"start\",\"number\":1,\"reference\":\"normal reference\"}");
  Serial.println("  {\"cmd\":\"end\",\"notes\":\"observed result\",\"physical\":\"not measured\"}");
  Serial.println("  {\"cmd\":\"calibration\",\"reference_v\":230.0}");
  Serial.println("  {\"cmd\":\"clear_trials\",\"confirmation\":\"EXPORTED\"}");
}
void loop() {
  if(!sharedMutex) { delay(100); return; }
  server.handleClient(); serviceSerial(); persistRecords(); pruneRecords(); serviceGraphs();
  static uint32_t retry=0; static bool wasConnected=false; uint32_t now=millis();
  bool connected=WiFi.status()==WL_CONNECTED;
  if(connected && !wasConnected) printNetworkStatus("reconnected");
  wasConnected=connected;
  if(!connected && now-retry>=WIFI_RETRY_MS) { retry=now; Serial.println("WiFi retry..."); WiFi.reconnect(); }
  delay(2);
}
