// RMS voltage monitoring; hardware identity/polarity and capture remain release gates.
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <time.h>
#include <lwip/sockets.h>
#include "config.h"
#include "recovery.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif
#include "protection.h"
#include "acquisition.h"
#include "events.h"
#include "storage.h"
#include "alert_history.h"
#include "trials.h"
#include "relay_persistence.h"
#include "webpage.h"

WebServer server(80);
// Live push stream; loop() owns the server and its clients.
WiFiServer streamServer(STREAM_PORT);
WiFiClient streamClients[STREAM_MAX_CLIENTS];
std::atomic<bool> statusDirty{false};
// Active ZMPT101B scale: CAL_DEFAULT until a dashboard calibration is saved or loaded.
std::atomic<float> voltageCal{CAL_DEFAULT};
Storage storage;
Acquisition acquisition;
Protection protection;
SemaphoreHandle_t sharedMutex;
TaskHandle_t acquisitionTaskHandle = nullptr;
char bootId[24];
uint32_t eventSequence = 0;
HistoryQueue graphQueue;
AlertHistory graphWork;  // Loop-owned; keep large copies off task stacks.
bool graphCleanup = true;
struct Published {
  Protection p;
  Measurement measurement;
  float rms = 0, current = 0, dvdt = 0;
  float voltageMin = 0, voltageMax = 0;
  bool haveVoltageRange = false;
  uint64_t sampleUs = 0, heartbeatUs = 0, maxGapUs = 0;
  int64_t sampleWall = 0;
  uint32_t revision = 1, eventRevision = 1, trialRevision = 1;
  bool armRequested = false, armReady = false;
  Event latest;
  // Dashboard snapshot: refreshed every DISPLAY_UPDATE_PERIOD_US, or at once on a surge or validity change.
  float displayRms = 0, displayCurrent = 0;
  bool displayValid = false, displayCurrentValid = false, displayCurrentEstimated = false, currentEstimated = false;
  uint64_t displayUs = 0;
  // Change detection against the steady level (baseline); changeUs is the last burst of the change.
  float step = 0, deviation = 0, baseline = 0, noise = 0, rawMean = 0;
  bool windowFull = false, changed = false;
  uint32_t changeCount = 0;
  uint64_t changeUs = 0;
  float changeFrom = 0, changeTo = 0;
} published;
Event eventLog[MAX_SURGE_ENTRIES];
bool relayTestActive = false, relayTestOff = false;
bool demoTripActive = false;
uint64_t demoTripUntil = 0;
// Surge test: a simulated RMS value is fed through the real detection path for DEMO_TRIP_DURATION_MS.
bool surgeTestRequested = false, surgeTestActive = false;
uint64_t surgeTestUntil = 0;
float surgeTestVolts = 0;
// Loop-owned snapshot reused by persistence and HTTP; avoids duplicate large buffers.
Event eventCopy[MAX_SURGE_ENTRIES];
int eventCount = 0;
HistoryPoint history[HISTORY_SIZE];
int historyHead = 0, historyCount = 0;
Trial trials[MAX_TRIALS];
int trialCount = 0, activeTrial = -1;
char sessionId[48] = {};
uint32_t savedState = 0, savedEvents = 0, savedTrials = 0;
uint64_t lastPersist = 0, lastPrune = 0;
bool taskStarted = false;
std::atomic<bool> wifiDisconnected{false};

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

void lockShared() {
  xSemaphoreTake(sharedMutex, portMAX_DELAY);
}
void unlockShared() {
  xSemaphoreGive(sharedMutex);
}
int64_t clockEpoch() {
  time_t t = time(nullptr);
  return t >= 1609459200 ? int64_t(t) : 0;
}
bool calInBounds(float cal) {
  return isfinite(cal) && cal >= CAL_DEFAULT * (1.0f - CAL_MAX_ADJUST_PCT / 100.0f) &&
         cal <= CAL_DEFAULT * (1.0f + CAL_MAX_ADJUST_PCT / 100.0f);
}
// Simulated load current for demos (DEMO_CURRENT_FROM_VOLTAGE); not a measurement.
float demoCurrentFromVoltage(float voltage) {
  float jitter = (float(int(esp_random() % 21)) - 10.0f) / 100.0f;
  float estimated = 1.5f * (voltage / NOMINAL_VOLTAGE_RMS) + jitter;
  if (voltage < NOMINAL_VOLTAGE_RMS) return constrain(estimated, 1.0f, 1.49f);
  return constrain(estimated, 1.5f, 2.5f);
}
int todayProtectionCount() {
  int64_t now = clockEpoch();
  if (!now) return -1;
  int64_t day = (now + GMT_OFFSET_S) / 86400;
  int count = 0;
  lockShared();
  for (int i = 0; i < eventCount; i++)
    if (eventLog[i].wall && (eventLog[i].wall + GMT_OFFSET_S) / 86400 == day && isProtection(eventLog[i].type)) count++;
  unlockShared();
  return count;
}
void commandLoad(bool on, bool manual = false) {
  // NC contactor: LOAD_ON_LEVEL releases the coil for load ON; LOAD_OFF_LEVEL energizes it for load OFF.
  // Manual dashboard control is not masked by the recovery hold; a real surge still cancels it.
  digitalWrite(RELAY_PIN, on && (manual || !recoveryHold.load()) ? LOAD_ON_LEVEL : LOAD_OFF_LEVEL);
}
// Processing/watchdog faults also end manual control so a repeated OFF cannot fight a manual ON.
void faultOff() {
  lockShared();
  relayTestActive = false;
  relayTestOff = false;
  unlockShared();
  commandLoad(false);
}
void updateLeds(const Protection& p) {
  bool red = p.state == RelayState::Lockout || (p.valid && p.surgeActive && p.voltage >= SURGE_THRESHOLD);
  bool green = !red && p.valid && p.voltage >= NORMAL_VOLTAGE_MIN_RMS && p.voltage <= NORMAL_VOLTAGE_MAX_RMS;
  bool amber = !red && p.valid && p.voltage > NORMAL_VOLTAGE_MAX_RMS;
  digitalWrite(LED_GREEN_PIN, LED_ACTIVE_HIGH ? green : !green);
  digitalWrite(LED_AMBER_PIN, LED_ACTIVE_HIGH ? amber : !amber);
  digitalWrite(LED_RED_PIN, LED_ACTIVE_HIGH ? red : !red);
}
// Called with sharedMutex after the command. Bounded RAM operations only.
void publishEvent(const Event& e) {
  if (eventCount == MAX_SURGE_ENTRIES) {
    memmove(eventLog, eventLog + 1, (MAX_SURGE_ENTRIES - 1) * sizeof(Event));
    eventCount--;
    graphCleanup = true;
  }
  eventLog[eventCount++] = e;
  published.eventRevision++;
  if (isProtection(e.type)) {
    graphQueue.push(e.id, history, historyHead, historyCount);
    published.latest = e;
    if (activeTrial >= 0) {
      Trial& t = trials[activeTrial];
      if (!t.detections) t.first = e;
      if (t.detections < 65535) t.detections++;
      published.trialRevision++;
    }
  }
}
Event makeEvent(EventType type, uint64_t trigger, uint64_t command, bool commanded, const Measurement& m, float rms) {
  Event e;
  lockShared();
  uint32_t sequence = ++eventSequence;
  unlockShared();
  snprintf(e.id, sizeof(e.id), "%s-%08lx", bootId, (unsigned long)sequence);
  strlcpy(e.boot, bootId, sizeof(e.boot));
  e.type = type;
  e.wall = clockEpoch();
  e.sampleUs = m.endUs;
  e.triggerUs = trigger;
  e.commandUs = command;
  e.commanded = commanded;
  e.rms = rms;
  e.valid = m.valid;
  return e;
}
void acquisitionOwner(void*) {
  const bool watched = recoveryWatchdogOK.load() && recoverySubscribe();
  if (!watched) {
    recoveryWatchdogOK.store(false);
    recoveryHold.store(true);
  }
  ResetButton reset;
  uint64_t next = 0;
  float filtered = 0, previousVoltage = 0;
  bool previousValid = false;
  float activeCal = voltageCal.load();
  float window[CHANGE_WINDOW_BURSTS], rawWindow[CHANGE_WINDOW_BURSTS];
  int windowHead = 0, windowCount = 0;
  uint64_t timeWindow[CHANGE_WINDOW_BURSTS];
  // Steady level and its scatter, held while a change is in progress.
  float stableLevel = 0, stableNoise = 0;
  bool haveStable = false, changing = false;
  int beyond = 0, flat = 0;
  Protection surgeTestSaved;
  double displaySum = 0;
  uint32_t displayCount = 0;
  bool surgeTestRealTrip = false, surgeTestManual = false, surgeTestManualOff = false;
  for (;;) {
    uint32_t loopCompleted = recoveryLoopMs.load();
    uint64_t now = esp_timer_get_time();
    if (now > PROCESS_FRESH_US && (!loopCompleted || uint32_t(now / 1000) - loopCompleted > PROCESS_FRESH_US / 1000)) {
      recoveryHold.store(true);
      recoveryHealthySinceMs.store(0);
      faultOff();
    }
    if (recoveryHold.load() && protection.state != RelayState::Lockout) {
      commandLoad(false);
      protection.restore(true, protection.tripCount, now);
      lockShared();
      published.p = protection;
      published.armRequested = false;
      published.armReady = false;
      published.revision++;
      unlockShared();
    }
    if (reset.update(digitalRead(RESET_PIN) == HIGH, now) && protection.state == RelayState::Lockout) {
      if (!recoveryCanAcknowledge(now)) continue;
      recoveryHold.store(false);
      protection.reset(now);
      commandLoad(false);
      updateLeds(protection);
      lockShared();
      published.p = protection;
      published.revision++;
      unlockShared();
    }
    if (now >= next) {
      Measurement m = acquisition.read();
      // Show each valid RMS burst without hiding isolated changes in the UI.
      // Protection uses this same RMS burst so a surge cannot be hidden by filtering.
      // A new calibration rescales every reading: restart the comparisons that span the change.
      float cal = voltageCal.load();
      bool recalibrated = cal != activeCal;
      if (recalibrated) {
        activeCal = cal;
        previousValid = false;
        windowHead = windowCount = 0;
        displaySum = 0;
        displayCount = 0;
        haveStable = changing = false;
        beyond = 0;
      }
      filtered = m.valid ? m.rawRms * cal : 0;
      float step = previousValid && m.valid ? filtered - previousVoltage : 0;
      // Rate of change is the least-squares trend over the window (~2 s): the difference of two
      // bursts is mostly reading jitter, several V/s even on a steady supply.
      float dvdt = 0, deviation = 0, rawMean = 0;
      bool windowFull = false, changeStarted = false, changeEnded = false;
      if (m.valid) {
        window[windowHead] = filtered;
        rawWindow[windowHead] = m.rawRms;
        timeWindow[windowHead] = m.endUs;
        windowHead = (windowHead + 1) % CHANGE_WINDOW_BURSTS;
        if (windowCount < CHANGE_WINDOW_BURSTS) windowCount++;
        double sum = 0, rawSum = 0, timeSum = 0, square = 0, timeSquare = 0, product = 0;
        for (int i = 0; i < windowCount; i++) {
          sum += window[i];
          rawSum += rawWindow[i];
          timeSum += double(m.endUs - timeWindow[i]) / -1000000.0;
        }
        double mean = sum / windowCount, timeMean = timeSum / windowCount;
        for (int i = 0; i < windowCount; i++) {
          double dv = window[i] - mean, dt = double(m.endUs - timeWindow[i]) / -1000000.0 - timeMean;
          square += dv * dv;
          timeSquare += dt * dt;
          product += dt * dv;
        }
        // Scatter around the trend line, so a ramp is not mistaken for reading noise.
        float scatter = 0;
        if (windowCount >= 3 && timeSquare > 0) {
          dvdt = float(product / timeSquare);
          scatter = float(sqrt(fmax(0.0, square - product * product / timeSquare) / windowCount));
        }
        rawMean = float(rawSum / windowCount);
        windowFull = windowCount == CHANGE_WINDOW_BURSTS;
        if (windowFull) {
          if (!haveStable) {
            stableLevel = float(mean);
            stableNoise = scatter;
            haveStable = true;
          }
          // A change starts when readings leave the steady level and ends once the window has
          // been flat for CHANGE_SETTLE_BURSTS; its mean is then the new steady level. A variac
          // sweep is therefore one change from where it started to where it stopped.
          float limit = fmaxf(CHANGE_MIN_V, CHANGE_NOISE_FACTOR * stableNoise);
          float drift = fabsf(dvdt) * float(double(m.endUs - timeWindow[windowHead]) / 1000000.0);
          deviation = filtered - stableLevel;
          if (!changing) {
            beyond = fabsf(deviation) >= limit ? beyond + 1 : 0;
            if (beyond >= CHANGE_CONFIRM_BURSTS) {
              changing = changeStarted = true;
              beyond = flat = 0;
            } else if (!beyond) stableNoise += 0.1f * (scatter - stableNoise);
          } else {
            flat = drift <= limit / 2 && scatter <= limit / 2 ? flat + 1 : 0;
            if (flat >= CHANGE_SETTLE_BURSTS) {
              changing = false;
              changeEnded = true;
              stableLevel = float(mean);
              deviation = filtered - stableLevel;
            }
          }
        }
      } else {
        windowHead = windowCount = 0;
        haveStable = changing = false;
        beyond = 0;
      }
      previousVoltage = filtered;
      previousValid = m.valid;
      // Current is only meaningful while the voltage measurement is valid.
      float currentAmps = m.currentRawRms * CURRENT_CAL_A_PER_V;
      if (currentAmps < CURRENT_NOISE_FLOOR_A) currentAmps = 0;
      // Measured current when valid; otherwise a labelled simulated value if DEMO_CURRENT_FROM_VOLTAGE.
      bool currentAvailable = m.valid && filtered > 1.0f && m.currentValid;
      bool currentEstimated = DEMO_CURRENT_FROM_VOLTAGE && m.valid && filtered > 1.0f && !currentAvailable;
      if (currentEstimated) currentAmps = demoCurrentFromVoltage(filtered);
      m.currentValid = currentAvailable;
      lockShared();
      bool testActive = relayTestActive, testOff = relayTestOff, demoActive = demoTripActive;
      uint64_t demoUntil = demoTripUntil;
      if (demoActive && now >= demoUntil) {
        demoTripActive = false;
        demoTripUntil = 0;
        demoActive = false;
      }
      bool surgeTestStart = surgeTestRequested && !surgeTestActive;
      if (surgeTestStart) {
        surgeTestRequested = false;
        surgeTestActive = true;
        surgeTestUntil = now + DEMO_TRIP_DURATION_MS * 1000ULL;
        surgeTestManual = relayTestActive;
        surgeTestManualOff = relayTestOff;
      }
      bool surgeTestEnd = surgeTestActive && !surgeTestStart && now >= surgeTestUntil;
      if (surgeTestEnd) surgeTestActive = false;
      bool surgeTest = surgeTestActive;
      float testVolts = surgeTestVolts;
      unlockShared();
      if (surgeTestStart) {
        surgeTestSaved = protection;
        surgeTestRealTrip = false;
      }
      // A finished test restores the pre-test protection state and manual mode, so it does not
      // count toward lockout; a real surge during the test keeps its trip.
      if (surgeTestEnd && !surgeTestRealTrip) {
        protection = surgeTestSaved;
        testActive = surgeTestManual;
        testOff = surgeTestManualOff;
        lockShared();
        relayTestActive = surgeTestManual;
        relayTestOff = surgeTestManualOff;
        published.revision++;
        unlockShared();
      }
      if (surgeTest && m.valid && filtered >= SURGE_THRESHOLD) surgeTestRealTrip = true;
      // The real reading still counts during a test: the higher of real and simulated is used.
      bool stepValid = surgeTest || m.valid;
      float stepVolts = surgeTest ? fmaxf(m.valid ? filtered : 0.0f, testVolts) : filtered;
      bool keepRelayOnForSensors = RELAY_POWERS_SENSORS && !testActive && protection.state != RelayState::Lockout;
      RelayState before = protection.state;
      uint8_t beforeCount = protection.tripCount;
      uint64_t trigger = esp_timer_get_time();
      // Boot readings are skipped while the sensor settles; the load stays connected.
      Decision decision = trigger < STARTUP_SETTLE_MS * 1000ULL && !surgeTest ? Decision{}
                          : protection.step(stepValid, stepVolts, trigger);
      // A confirmed surge cancels manual ON and sensor-power overrides immediately.
      bool surgeTrip = protection.valid && protection.surgeActive;
      if (surgeTrip) {
        testActive = false;
        keepRelayOnForSensors = false;
      }
      uint64_t command = 0;
      // No logging, locking, JSON, Serial or filesystem before the OFF command.
      if (decision.commandOff && !testActive && !demoActive && !keepRelayOnForSensors) {
        commandLoad(false);
        command = esp_timer_get_time();
      }
      // Reserve a possible next trip durably before energizing. Power loss before a
      // deferred OFF-state commit then restores the conservative reserved count.
      lockShared();
      if (surgeTrip) {
        relayTestActive = false;
        relayTestOff = false;
      }
      if (decision.commandOff && !testActive && !demoActive && !keepRelayOnForSensors) {
        published.armRequested = false;
        published.armReady = false;
      }
      // Reconnect waits for the trip count to be saved; when storage is unmounted or failing
      // nothing can be saved, so reconnect after voltage qualification alone instead of never.
      if (decision.commandOn && !testActive && !published.armReady && storage.mounted && storage.healthy) {
        protection.state = RelayState::Checking;
        decision.commandOn = false;
        if (!published.armRequested) {
          published.armRequested = true;
          published.revision++;
        }
      }
      unlockShared();
      if (demoActive) commandLoad(false);
      else if (testActive) commandLoad(!testOff, true);
      else if (keepRelayOnForSensors) commandLoad(true);
      else commandLoad(protection.state == RelayState::Normal);
      updateLeds(protection);
      Event e;
      if (decision.event != EventType::None) {
        e = makeEvent(decision.event, trigger, command, decision.commandOff && isProtection(decision.event), m, stepVolts);
        e.simulated = surgeTest;
        if (surgeTest) e.valid = true;
      }
      int64_t wall = clockEpoch();
      lockShared();
      published.p = protection;
      published.measurement = m;
      published.rms = filtered;
      published.dvdt = dvdt;
      published.step = step;
      published.deviation = deviation;
      published.baseline = haveStable ? stableLevel : 0;
      published.noise = stableNoise;
      published.rawMean = rawMean;
      published.windowFull = windowFull;
      published.changed = changing;
      if (changeStarted) {
        published.changeCount++;
        published.changeFrom = stableLevel;
      }
      // While the voltage is still moving the change runs to the latest reading; once it
      // settles it ends at the new steady level.
      if (changing) published.changeTo = filtered;
      if (changeEnded) published.changeTo = stableLevel;
      if (changing || changeEnded) published.changeUs = m.endUs;
      if (recalibrated) published.haveVoltageRange = false;
      if (m.valid) {
        if (!published.haveVoltageRange) {
          published.voltageMin = published.voltageMax = filtered;
          published.haveVoltageRange = true;
        } else {
          if (filtered < published.voltageMin) published.voltageMin = filtered;
          if (filtered > published.voltageMax) published.voltageMax = filtered;
        }
      }
      published.current = (currentAvailable || currentEstimated) ? currentAmps : 0;
      published.currentEstimated = currentEstimated;
      published.sampleUs = m.endUs;
      published.sampleWall = wall;
      if (m.gapUs > published.maxGapUs) published.maxGapUs = m.gapUs;
      if (before != protection.state || beforeCount != protection.tripCount) published.revision++;
      HistoryPoint point = { m.endUs, wall, filtered, published.current, dvdt, m.valid, currentAvailable || currentEstimated, currentEstimated };
      history[historyHead] = point;
      historyHead = (historyHead + 1) % HISTORY_SIZE;
      if (historyCount < HISTORY_SIZE) historyCount++;
      if (m.valid) { displaySum += filtered; displayCount++; }
      if (!published.displayUs || m.endUs - published.displayUs >= DISPLAY_UPDATE_PERIOD_US ||
          m.valid != published.displayValid || isProtection(e.type)) {
        // Show the mean of the window's valid bursts; a surge shows its own trip reading.
        float shown = isProtection(e.type) || !displayCount ? filtered : float(displaySum / displayCount);
        displaySum = 0;
        displayCount = 0;
        published.displayUs = m.endUs;
        published.displayRms = shown;
        published.displayCurrent = published.current;
        published.displayValid = m.valid;
        published.displayCurrentValid = currentAvailable;
        published.displayCurrentEstimated = currentEstimated;
      }
      if (activeTrial >= 0) {
        Trial& t = trials[activeTrial];
        if (!t.sampleCount) t.firstSampleUs = m.endUs;
        t.sampleCount++;
        if (!m.valid) t.invalidSamples++;
        t.lastSampleUs = m.endUs;
        if (m.gapUs > t.maxGapUs) t.maxGapUs = m.gapUs;
      }
      if (isProtection(e.type)) publishEvent(e);
      unlockShared();
      // Invalid sensor readings are handled protection outcomes, not firmware lockups.
      // Feed only after acquisition, OFF decisions and publication have completed.
      if (watched && esp_task_wdt_reset() != ESP_OK) {
        recoveryWatchdogOK.store(false);
        recoveryHold.store(true);
        faultOff();
      }
      next = now + VOLTAGE_SAMPLE_PERIOD_US;
    }
    lockShared();
    published.heartbeatUs = esp_timer_get_time();
    unlockShared();
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void loadRecords() {
  JsonDocument doc;
  bool damaged = false;
  bool loaded = storage.load(RELAY_PATH, doc, 2048, damaged);
  RestoredRelay restored = decodeRelay(doc.as<JsonObjectConst>(), loaded, damaged, storage.mounted);
  if (!restored.schemaValid) {
    storage.fail();
    storage.quarantine(RELAY_PATH);
  }
  protection.restore(restored.lockout, restored.trips, esp_timer_get_time());
  published.p = protection;
  doc.clear();
  if (storage.load(SURGE_PATH, doc, EVENT_FILE_BYTE_CAP, damaged)) {
    JsonArray array = doc["surges"].as<JsonArray>();
    if (array.isNull() || array.size() > MAX_SURGE_ENTRIES) {
      storage.fail();
      storage.quarantine(SURGE_PATH);
    } else
      for (JsonObject o : array) {
        Event e;
        if (o["id"].isNull()) {
          char id[48];
          snprintf(id, sizeof(id), "legacy-%lld-%d", (long long)(o["ts"] | int64_t(0)), eventCount);
          o["id"] = id;
          o["boot_id"] = "legacy";
          if (o["t"].isNull()) o["t"] = "SURGE";
        }
        if (readEvent(o, e)) {
          if (isProtection(e.type)) {
            eventLog[eventCount++] = e;
            published.latest = e;
          }
        } else {
          storage.fail();
          storage.quarantine(SURGE_PATH);
        }
      }
  }
  doc.clear();
  if (storage.load("/trials.json", doc, TRIAL_FILE_BYTE_CAP, damaged)) {
    JsonArray array = doc["trials"].as<JsonArray>();
    if (array.isNull() || array.size() > MAX_TRIALS) {
      storage.fail();
      storage.quarantine("/trials.json");
    } else
      for (JsonObject o : array) {
        Trial t;
        if (readTrial(o, t)) trials[trialCount++] = t;
        else {
          storage.fail();
          storage.quarantine("/trials.json");
        }
      }
  }
  doc.clear();
  if (storage.load(CAL_PATH, doc, 256, damaged)) {
    float cal = doc["cal"] | 0.0f;
    if ((doc["schema"] | 0) == 1 && calInBounds(cal)) voltageCal.store(cal);
    else {
      storage.fail();
      storage.quarantine(CAL_PATH);
    }
  }
}
void persistRecords() {
  uint64_t now = esp_timer_get_time();
  if (now - lastPersist < 5000000ULL) return;
  lastPersist = now;
  Published snap;
  lockShared();
  snap = published;
  unlockShared();
  JsonDocument doc;
  if (savedState != snap.revision) {
    encodeRelay(doc, snap.p, snap.armRequested);
    if (storage.commit(RELAY_PATH, doc, 2048)) {
      savedState = snap.revision;
      lockShared();
      if (published.revision == snap.revision && published.armRequested) published.armReady = true;
      unlockShared();
    } else return;
  }
  if (savedEvents != snap.eventRevision) {
    int count;
    lockShared();
    count = eventCount;
    memcpy(eventCopy, eventLog, count * sizeof(Event));
    uint32_t rev = published.eventRevision;
    unlockShared();
    File f = storage.start(SURGE_PATH);
    const char* prefix = "{\"schema\":2,\"surges\":[";
    size_t expected = strlen(prefix);
    bool good = f && f.print(prefix) == expected;
    for (int i = 0; i < count && good; i++) {
      if (i) {
        good = f.print(',') == 1;
        expected++;
      }
      doc.clear();
      eventRecord(doc.to<JsonObject>(), eventCopy[i]);
      size_t n = measureJson(doc);
      expected += n;
      good = good && expected + 2 <= EVENT_FILE_BYTE_CAP && !doc.overflowed() && serializeJson(doc, f) == n;
    }
    expected += 2;
    good = good && f.print("]}") == 2;
    if (storage.finish(SURGE_PATH, f, expected, EVENT_FILE_BYTE_CAP, good)) savedEvents = rev;
  }
  if (savedTrials != snap.trialRevision) {
    doc.clear();
    int count;
    uint32_t rev;
    lockShared();
    count = trialCount;
    rev = published.trialRevision;
    unlockShared();
    File f = storage.start("/trials.json");
    const char* prefix = "{\"schema\":1,\"trials\":[";
    size_t expected = strlen(prefix);
    bool good = f && f.print(prefix) == expected;
    for (int i = 0; i < count && good; i++) {
      Trial t;
      lockShared();
      t = trials[i];
      unlockShared();
      if (i) {
        good = f.print(',') == 1;
        expected++;
      }
      doc.clear();
      trialJson(doc.to<JsonObject>(), t);
      size_t n = measureJson(doc);
      expected += n;
      good = good && expected + 2 <= TRIAL_FILE_BYTE_CAP && !doc.overflowed() && serializeJson(doc, f) == n;
    }
    expected += 2;
    good = good && f.print("]}") == 2;
    if (storage.finish("/trials.json", f, expected, TRIAL_FILE_BYTE_CAP, good)) savedTrials = rev;
  }
}
void pruneRecords() {
  uint64_t now = esp_timer_get_time();
  if (now - lastPrune < 60000000ULL) return;
  lastPrune = now;
  int64_t wall = clockEpoch();
  if (!wall) return;
  lockShared();
  int count = 0;
  for (int i = 0; i < eventCount; i++)
    if (!eventLog[i].wall || eventLog[i].wall >= wall - SURGE_RETAIN_DAYS * 86400LL) eventLog[count++] = eventLog[i];
  if (count != eventCount) {
    eventCount = count;
    published.eventRevision++;
    graphCleanup = true;
  }
  unlockShared();
}
// Filesystem work only on loop(), after acquisition has issued relay commands.
void serviceGraphs() {
  lockShared();
  bool have = graphQueue.pop(graphWork);
  bool retained = false;
  if (have)
    for (int i = 0; i < eventCount; i++)
      if (!strcmp(eventLog[i].id, graphWork.id)) retained = true;
  bool cleanup = graphCleanup;
  graphCleanup = false;
  unlockShared();
  if (have && retained) saveGraph(storage, graphWork);
  if (!cleanup || !storage.mounted) return;
  int count;
  lockShared();
  count = eventCount;
  memcpy(eventCopy, eventLog, count * sizeof(Event));
  unlockShared();
  File root = LittleFS.open("/");
  if (!root) {
    storage.fail();
    lockShared();
    graphCleanup = true;
    unlockShared();
    return;
  }
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String path = f.name();
    f.close();
    if (!path.startsWith("/")) path = "/" + path;
    if (!path.startsWith("/graph-")) continue;
    if (!cleanGraphFile(storage, path, eventCopy, count)) {
      lockShared();
      graphCleanup = true;
      unlockShared();
    }
  }
  root.close();
}
void sendJson(JsonDocument& doc, int code = 200) {
  if (doc.overflowed()) {
    server.send(503, "application/json", "{\"error\":\"allocation_failed\"}");
    return;
  }
  String body;
  serializeJson(doc, body);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}
// Dashboard state. The pushed status event carries the short form (full = false) so it always
// fits one non-blocking send; GET /data adds the diagnostics.
void buildStatus(JsonDocument& doc, bool full) {
  Published p;
  lockShared();
  p = published;
  unlockShared();
  uint64_t now = esp_timer_get_time();
  bool displayVoltage = p.displayValid && p.displayUs && p.displayRms >= DISPLAY_VOLTAGE_MIN_RMS;
  if (displayVoltage) doc["voltage"] = p.displayRms;
  else doc["voltage"] = nullptr;
  if (full) {
    doc["display_update_period_us"] = DISPLAY_UPDATE_PERIOD_US;
    if (p.displayUs) doc["display_age_ms"] = (now - p.displayUs) / 1000;
    else doc["display_age_ms"] = nullptr;
    doc["voltage_sample_period_us"] = VOLTAGE_SAMPLE_PERIOD_US;
    doc["current_demo_mode"] = DEMO_CURRENT_FROM_VOLTAGE;
    doc["current_sensor_pin"] = CURRENT_PIN;
    doc["current_cal_a_per_v"] = CURRENT_CAL_A_PER_V;
    doc["current_adc_min"] = p.measurement.currentMinimum;
    doc["current_adc_max"] = p.measurement.currentMaximum;
    doc["timestamp_us"] = p.sampleUs;  // Compatibility alias now explicitly describes acquisition.
    doc["voltageClass"] = p.p.classification();
  }
  float cal = voltageCal.load();
  doc["voltage_cal"] = cal;
  doc["voltage_cal_default"] = CAL_DEFAULT;
  doc["voltage_cal_custom"] = cal != CAL_DEFAULT;
  doc["cal_reference_min"] = CAL_REFERENCE_MIN_V;
  doc["cal_reference_max"] = CAL_REFERENCE_MAX_V;
  doc["event_revision"] = p.eventRevision;
  if (p.haveVoltageRange) {
    doc["voltage_min_since_boot"] = p.voltageMin;
    doc["voltage_max_since_boot"] = p.voltageMax;
  } else {
    doc["voltage_min_since_boot"] = nullptr;
    doc["voltage_max_since_boot"] = nullptr;
  }
  bool currentShown = p.displayCurrentValid || (p.displayCurrentEstimated && p.displayCurrent > 0);
  if (displayVoltage && currentShown) doc["current"] = p.displayCurrent;
  else doc["current"] = nullptr;
  if (displayVoltage && currentShown) doc["power"] = p.displayRms * p.displayCurrent;
  else doc["power"] = nullptr;
  doc["current_estimated"] = p.displayCurrentEstimated;
  doc["current_sensor_ok"] = p.measurement.currentValid;
  doc["current_adc_mean"] = p.measurement.currentMean;
  doc["dvdt"] = p.dvdt;
  if (!p.measurement.valid) doc["dvdt"] = nullptr;
  doc["status"] = p.p.classification();
  doc["nominal"] = NOMINAL_VOLTAGE_RMS;
  doc["vmin"] = NORMAL_VOLTAGE_MIN_RMS;
  doc["vmax"] = NORMAL_VOLTAGE_MAX_RMS;
  int today = todayProtectionCount();
  if (today >= 0) doc["todaySurges"] = today;
  else doc["todaySurges"] = nullptr;
  doc["wifi_rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["sensorOk"] = p.measurement.valid;
  doc["systemState"] = p.p.systemState();
  doc["fault_reason"] = p.p.reason();
  doc["relayState"] = relayName(p.p.state);
  lockShared();
  doc["relay_test_off"] = relayTestOff;
  doc["relay_test_active"] = relayTestActive;
  doc["relay_test_on"] = relayTestActive && !relayTestOff;
  doc["demo_trip_active"] = demoTripActive;
  if (demoTripActive && demoTripUntil > now) doc["demo_trip_remaining_ms"] = (demoTripUntil - now) / 1000;
  else doc["demo_trip_remaining_ms"] = 0;
  doc["surge_test_active"] = surgeTestActive || surgeTestRequested;
  if (surgeTestActive || surgeTestRequested) doc["surge_test_v"] = surgeTestVolts;
  else doc["surge_test_v"] = nullptr;
  if (surgeTestActive && surgeTestUntil > now) doc["surge_test_remaining_ms"] = (surgeTestUntil - now) / 1000;
  else doc["surge_test_remaining_ms"] = 0;
  // Actual relay pin state drives the dashboard alert: relay ON means the load is isolated.
  bool relayOn = digitalRead(RELAY_PIN) == LOAD_OFF_LEVEL;
  doc["relay_on"] = relayOn;
  if (!relayOn) doc["isolation_reason"] = nullptr;
  else if (demoTripActive) doc["isolation_reason"] = "SURGE_DEMO";
  else if (surgeTestActive) doc["isolation_reason"] = "SURGE_TEST";
  else if (p.p.surgeActive) doc["isolation_reason"] = "SURGE";
  else if (p.p.underActive) doc["isolation_reason"] = "UNDERVOLTAGE";
  else if (relayTestActive && relayTestOff) doc["isolation_reason"] = "MANUAL_OFF";
  else doc["isolation_reason"] = p.p.reason();
  unlockShared();
  doc["boot_id"] = bootId;
  doc["uptime"] = now / 1000000;
  if (!full) {
    if (p.latest.id[0]) {
      doc["latest_event"]["id"] = p.latest.id;
      doc["latest_event"]["t"] = eventName(p.latest.type);
    } else doc["latest_event"] = nullptr;
    return;
  }
  doc["relay_powers_sensors"] = RELAY_POWERS_SENSORS;
  doc["sample_us"] = p.sampleUs;
  if (p.sampleUs) doc["sample_age_ms"] = (now - p.sampleUs) / 1000;
  else doc["sample_age_ms"] = nullptr;
  if (p.sampleWall) doc["sample_epoch"] = p.sampleWall;
  else doc["sample_epoch"] = nullptr;
  doc["sample_clock_valid"] = p.sampleWall != 0;
  doc["ntp"] = clockEpoch() != 0;
  doc["storage_ok"] = storage.healthy;
  doc["storage_mounted"] = storage.mounted;
  doc["storage_failures"] = storage.failures;
  doc["reconnect_storage_pending"] = p.armRequested && !p.armReady;
  doc["heap_free"] = ESP.getFreeHeap();
  doc["heap_min"] = ESP.getMinFreeHeap();
  doc["heap_largest"] = ESP.getMaxAllocHeap();
  doc["task_started"] = taskStarted;
  doc["task_heartbeat_age_ms"] = (now - p.heartbeatUs) / 1000;
  doc["task_stack_high_water"] = acquisitionTaskHandle ? uxTaskGetStackHighWaterMark(acquisitionTaskHandle) : 0;
  doc["reset_reason"] = int(esp_reset_reason());
  doc["reset_reason_name"] = resetReasonName(esp_reset_reason());
  doc["recovery_hold"] = recoveryHold.load();
  doc["recovery_crash_loop"] = recoveryCrashLoop;
  doc["watchdog_ok"] = recoveryWatchdogOK.load();
  doc["retained_boot_count"] = recoveryBoots;
  doc["unstable_boot_count"] = recoveryStreak;
  doc["burst_duration_us"] = p.measurement.endUs - p.measurement.startUs;
  doc["inter_burst_gap_us"] = p.measurement.gapUs;
  doc["max_inter_burst_gap_us"] = p.maxGapUs;
  doc["max_sample_interval_us"] = p.measurement.maxIntervalUs;
  doc["adc_samples"] = ADC_SAMPLES;
  doc["raw_rms_adc_volts"] = p.measurement.rawRms;
  doc["adc_min"] = p.measurement.minimum;
  doc["adc_max"] = p.measurement.maximum;
  doc["voltage_sensor_reason"] = p.measurement.invalidReason();
  doc["voltage_adc_mean"] = p.measurement.mean;
  doc["voltage_adc_mean_drift"] = p.measurement.meanDrift;
  doc["voltage_zero_crossings"] = p.measurement.crossings;
  doc["voltage_rms_window_samples"] = p.measurement.rmsSamples;
  doc["voltage_frequency_hz"] = p.measurement.frequencyHz;
  doc["voltage_step"] = p.step;
  doc["voltage_baseline"] = p.baseline;
  doc["voltage_deviation"] = p.deviation;
  doc["voltage_noise"] = p.noise;
  doc["voltage_changed"] = p.changed;
  doc["voltage_change_count"] = p.changeCount;
  if (p.measurement.valid && p.measurement.rawRms > 0.0f) doc["voltage_scale_applied"] = p.rms / p.measurement.rawRms;
  else doc["voltage_scale_applied"] = nullptr;
  doc["capture_enabled"] = false;
  doc["capture_status"] = "disabled_pending_hardware_characterization";
  if (p.latest.id[0]) eventJson(doc["latest_event"].to<JsonObject>(), p.latest);
  else doc["latest_event"] = nullptr;
}
void handleData() {
  JsonDocument doc;
  buildStatus(doc, true);
  sendJson(doc);
}
// A stalled browser must never stall loop(): that would look like a processing fault and
// isolate the load. A frame that cannot be queued whole drops the client; the page reconnects.
bool streamSend(WiFiClient& client, const char* data, size_t length) {
  int fd = client.fd();
  if (fd >= 0 && lwip_send(fd, data, length, MSG_DONTWAIT) == ssize_t(length)) return true;
  client.stop();
  return false;
}
void streamNumber(char* out, size_t size, bool present, float value) {
  if (present && isfinite(value)) snprintf(out, size, "%.2f", value);
  else strlcpy(out, "null", size);
}
// The recent per-burst readings, so a newly opened page does not start with empty charts.
void streamHistory(WiFiClient& client) {
  static HistoryPoint copy[HISTORY_SIZE];
  int count;
  lockShared();
  count = historyCount;
  for (int i = 0; i < count; i++) copy[i] = history[(historyHead - count + HISTORY_SIZE + i) % HISTORY_SIZE];
  unlockShared();
  String voltages, currents;
  voltages.reserve(count * 8);
  currents.reserve(count * 6);
  char number[16];
  for (int i = 0; i < count; i++) {
    bool shown = copy[i].valid && copy[i].voltage >= DISPLAY_VOLTAGE_MIN_RMS;
    streamNumber(number, sizeof(number), shown, copy[i].voltage);
    if (i) voltages += ',';
    voltages += number;
    streamNumber(number, sizeof(number), shown && copy[i].currentValid, copy[i].current);
    if (i) currents += ',';
    currents += number;
  }
  String frame = "event: history\ndata: {\"v\":[" + voltages + "],\"i\":[" + currents + "]}\n\n";
  streamSend(client, frame.c_str(), frame.length());
}
void serviceStream() {
  static uint64_t pushedSample = 0;
  static uint32_t pushedRevision = 0, pushedEvents = 0, lastStatus = 0;
  static bool pushedRelayOn = false;
  WiFiClient incoming = streamServer.accept();
  if (incoming) {
    // Bounded wait for the end of the request headers; the reply does not depend on them.
    uint32_t start = millis();
    int matched = 0;
    while (matched < 4 && millis() - start < STREAM_REQUEST_WAIT_MS) {
      if (!incoming.available()) {
        delay(1);
        continue;
      }
      char c = incoming.read();
      matched = c == "\r\n\r\n"[matched] ? matched + 1 : c == '\r';
    }
    int slot = -1;
    for (int i = 0; i < STREAM_MAX_CLIENTS && slot < 0; i++)
      if (streamClients[i].fd() < 0 || !streamClients[i].connected()) slot = i;
    if (matched < 4 || slot < 0) incoming.stop();
    else {
      incoming.setNoDelay(true);
      streamClients[slot].stop();
      streamClients[slot] = incoming;
      static const char headers[] = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-store\r\n"
                                    "Access-Control-Allow-Origin: *\r\nConnection: keep-alive\r\n\r\nretry: 2000\n\n";
      if (streamSend(streamClients[slot], headers, sizeof(headers) - 1)) streamHistory(streamClients[slot]);
      statusDirty.store(true);
    }
  }
  bool listening = false;
  for (int i = 0; i < STREAM_MAX_CLIENTS; i++)
    if (streamClients[i].fd() >= 0) listening = true;
  if (!listening) return;
  uint64_t sample;
  uint32_t revision, events;
  lockShared();
  sample = published.sampleUs;
  revision = published.revision;
  events = published.eventRevision;
  unlockShared();
  if (sample != pushedSample) {
    pushedSample = sample;
    Published p;
    lockShared();
    p = published;
    unlockShared();
    uint64_t now = esp_timer_get_time();
    bool shown = p.measurement.valid && p.rms >= DISPLAY_VOLTAGE_MIN_RMS;
    bool currentShown = shown && p.current > 0;
    char v[16], average[16], step[16], deviation[16], baseline[16], noise[16], hz[16], amps[16], watts[16], dvdt[16], from[16], to[16], age[16];
    streamNumber(v, sizeof(v), shown, p.rms);
    streamNumber(average, sizeof(average), p.displayValid && p.displayRms >= DISPLAY_VOLTAGE_MIN_RMS, p.displayRms);
    streamNumber(step, sizeof(step), shown, p.step);
    streamNumber(deviation, sizeof(deviation), shown && p.baseline > 0, p.deviation);
    streamNumber(baseline, sizeof(baseline), shown && p.baseline > 0, p.baseline);
    streamNumber(noise, sizeof(noise), shown && p.baseline > 0, p.noise);
    streamNumber(hz, sizeof(hz), shown && p.measurement.frequencyHz > 0, p.measurement.frequencyHz);
    streamNumber(amps, sizeof(amps), currentShown, p.current);
    streamNumber(watts, sizeof(watts), currentShown, p.rms * p.current);
    streamNumber(dvdt, sizeof(dvdt), shown, p.dvdt);
    streamNumber(from, sizeof(from), p.changeCount, p.changeFrom);
    streamNumber(to, sizeof(to), p.changeCount, p.changeTo);
    if (p.changeCount) snprintf(age, sizeof(age), "%lu", (unsigned long)((now - p.changeUs) / 1000));
    else strlcpy(age, "null", sizeof(age));
    char frame[512];
    int length = snprintf(frame, sizeof(frame),
                          "event: live\ndata: {\"v\":%s,\"avg\":%s,\"step\":%s,\"dev\":%s,\"base\":%s,\"noise\":%s,\"hz\":%s,"
                          "\"i\":%s,\"p\":%s,\"i_est\":%s,\"dvdt\":%s,\"chg\":%s,\"chgs\":%lu,\"chg_age_ms\":%s,\"chg_from\":%s,\"chg_to\":%s,"
                          "\"ok\":%s,\"sys\":\"%s\",\"relay\":\"%s\"}\n\n",
                          v, average, step, deviation, baseline, noise, hz, amps, watts, p.currentEstimated ? "true" : "false", dvdt,
                          p.changed ? "true" : "false", (unsigned long)p.changeCount, age, from, to,
                          p.measurement.valid ? "true" : "false", p.p.systemState(), relayName(p.p.state));
    if (length > 0 && length < int(sizeof(frame)))
      for (int i = 0; i < STREAM_MAX_CLIENTS; i++)
        if (streamClients[i].fd() >= 0) streamSend(streamClients[i], frame, length);
  }
  uint32_t nowMs = millis();
  bool relayOn = digitalRead(RELAY_PIN) == LOAD_OFF_LEVEL;
  if (statusDirty.exchange(false) || revision != pushedRevision || events != pushedEvents || relayOn != pushedRelayOn ||
      nowMs - lastStatus >= STREAM_STATUS_PERIOD_MS) {
    pushedRevision = revision;
    pushedEvents = events;
    pushedRelayOn = relayOn;
    lastStatus = nowMs;
    JsonDocument doc;
    buildStatus(doc, false);
    if (doc.overflowed()) return;
    String body;
    serializeJson(doc, body);
    String frame = "event: status\ndata: " + body + "\n\n";
    for (int i = 0; i < STREAM_MAX_CLIENTS; i++)
      if (streamClients[i].fd() >= 0) streamSend(streamClients[i], frame.c_str(), frame.length());
  }
}
void handleSurges() {
  int count;
  lockShared();
  count = eventCount;
  memcpy(eventCopy, eventLog, count * sizeof(Event));
  unlockShared();
  JsonDocument doc;
  doc["count"] = count;
  doc["boot_id"] = bootId;
  int today = todayProtectionCount();
  if (today >= 0) doc["todayCount"] = today;
  else doc["todayCount"] = nullptr;
  String since = server.arg("since");
  bool found = since.isEmpty();
  int after = -1;
  for (int i = 0; i < count; i++)
    if (since == eventCopy[i].id) {
      found = true;
      after = i;
    }
  doc["retention_gap"] = !found;
  doc["oldest_id"] = count ? eventCopy[0].id : "";
  doc["newest_id"] = count ? eventCopy[count - 1].id : "";
  String prefix;
  serializeJson(doc, prefix);
  prefix.remove(prefix.length() - 1);
  prefix += ",\"entries\":[";
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(prefix);
  if (server.arg("all") == "1") after = -1;
  for (int i = count - 1; i > after; i--) {
    doc.clear();
    eventJson(doc.to<JsonObject>(), eventCopy[i]);
    String body;
    serializeJson(doc, body);
    if (i < count - 1) server.sendContent(",");
    server.sendContent(body);
  }
  server.sendContent("]}");
  server.sendContent("");
}
void handleEvent() {
  String id = server.arg("id");
  Event e;
  bool found = false;
  lockShared();
  for (int i = 0; i < eventCount; i++)
    if (id == eventLog[i].id) {
      e = eventLog[i];
      found = true;
      break;
    }
  unlockShared();
  JsonDocument doc;
  if (found) {
    eventJson(doc.to<JsonObject>(), e);
    doc["samples"].to<JsonArray>();
    JsonObject h = doc["history"].to<JsonObject>();
    bool pending = false;
    lockShared();
    for (unsigned i = 0; i < graphQueue.count; i++)
      if (id == graphQueue.slots[(graphQueue.head + i) % 4].id) pending = true;
    unlockShared();
    if (loadGraph(storage, e.id, graphWork)) historyJson(h, graphWork);
    else {
      h["status"] = pending ? "pending" : "unavailable";
      h["samples"].to<JsonArray>();
    }
  } else doc["error"] = "event_not_retained";
  sendJson(doc, found ? 200 : 404);
}
void handleClearLogs() {
  if (!strlen(CLEAR_TOKEN) || !server.hasArg("token") || server.arg("token") != CLEAR_TOKEN) {
    server.send(403, "application/json", "{\"error\":\"disabled_or_bad_token\"}");
    return;
  }
  uint32_t rev;
  lockShared();
  rev = published.eventRevision;
  unlockShared();
  JsonDocument doc;
  doc["schema"] = 2;
  doc["surges"].to<JsonArray>();
  bool stored = storage.commit(SURGE_PATH, doc, EVENT_FILE_BYTE_CAP);
  lockShared();
  if (published.eventRevision == rev) {
    eventCount = 0;
    published.eventRevision++;
    savedEvents = stored ? published.eventRevision : 0;
    graphQueue.count = 0;
    graphCleanup = true;
  } else {
    savedEvents = 0;
    unlockShared();
    server.send(409, "application/json", "{\"error\":\"new_event_arrived_retry\"}");
    return;
  }
  published.latest = Event{};
  unlockShared();
  if (stored) server.send(200, "application/json", "{\"ok\":true}");
  else server.send(200, "application/json", "{\"ok\":true,\"warning\":\"storage_write_failed_ram_cleared\"}");
}
void handleRelayTest() {
  bool automatic = server.hasArg("auto") && server.arg("auto") == "1";
  bool off = server.hasArg("off") && server.arg("off") == "1";
  if (!automatic && (!server.hasArg("off") || (server.arg("off") != "0" && server.arg("off") != "1"))) {
    server.send(400, "application/json", "{\"error\":\"use_off_0_or_1_or_auto_1\"}");
    return;
  }
  lockShared();
  relayTestActive = !automatic;
  relayTestOff = !automatic && off;
  if (!automatic) {
    demoTripActive = false;
    demoTripUntil = 0;
  }
  unlockShared();
  statusDirty.store(true);
  // The acquisition task applies the override and is the sole runtime GPIO owner.
  server.send(200, "application/json", "{\"ok\":true}");
}
// Dashboard calibration: reference_v is the user's meter reading; reset=1 returns to CAL_DEFAULT.
// The new scale applies to the display and to protection from the next burst.
void handleCalibrate() {
  float previous = voltageCal.load(), cal = CAL_DEFAULT, before = 0, reference = 0;
  bool reset = server.arg("reset") == "1", stored = false;
  if (reset) {
    String backup = String(CAL_PATH) + ".bak";
    stored = storage.mounted && (!LittleFS.exists(CAL_PATH) || LittleFS.remove(CAL_PATH)) &&
             (!LittleFS.exists(backup) || LittleFS.remove(backup));
  } else {
    reference = server.arg("reference_v").toFloat();
    if (!(reference > CAL_REFERENCE_MIN_V && reference <= CAL_REFERENCE_MAX_V)) {
      server.send(400, "application/json", "{\"error\":\"reference_out_of_range\"}");
      return;
    }
    Published p;
    lockShared();
    p = published;
    bool testing = demoTripActive || surgeTestActive || surgeTestRequested;
    unlockShared();
    if (testing) {
      server.send(409, "application/json", "{\"error\":\"test_running\"}");
      return;
    }
    // The mean of the last ~2 s of bursts, not one burst, so reading jitter does not end up in the scale.
    if (!p.measurement.valid || !p.windowFull || p.rawMean <= 0.0f || esp_timer_get_time() - p.sampleUs > PROCESS_FRESH_US) {
      server.send(409, "application/json", "{\"error\":\"no_steady_reading\"}");
      return;
    }
    cal = reference / p.rawMean;
    before = p.rawMean * previous;
    if (!calInBounds(cal)) {
      server.send(400, "application/json", "{\"error\":\"scale_out_of_bounds\"}");
      return;
    }
    JsonDocument saved;
    saved["schema"] = 1;
    saved["cal"] = cal;
    saved["reference_v"] = reference;
    stored = storage.commit(CAL_PATH, saved, 256);
  }
  voltageCal.store(cal);
  statusDirty.store(true);
  Serial.printf("CALIBRATION scale %.4f -> %.4f (%s), saved=%d\n", previous, cal, reset ? "reset to CAL_DEFAULT" : "dashboard meter reading", stored);
  JsonDocument doc;
  doc["ok"] = true;
  doc["cal"] = cal;
  doc["previous_cal"] = previous;
  doc["stored"] = stored;
  if (reset) {
    doc["reading_before"] = nullptr;
    doc["reading_after"] = nullptr;
  } else {
    doc["reading_before"] = before;
    doc["reading_after"] = reference;
  }
  sendJson(doc);
}
void handleSimulateSurge() {
  uint64_t trigger = esp_timer_get_time();
  commandLoad(false);
  uint64_t command = esp_timer_get_time();
  Published p;
  lockShared();
  p = published;
  relayTestActive = false;
  relayTestOff = false;
  demoTripActive = true;
  demoTripUntil = trigger + DEMO_TRIP_DURATION_MS * 1000ULL;
  unlockShared();
  Event e = makeEvent(EventType::Surge, trigger, command, true, p.measurement, p.rms);
  e.simulated = true;
  lockShared();
  publishEvent(e);
  published.latest = e;
  unlockShared();
  statusDirty.store(true);
  server.send(200, "application/json", "{\"ok\":true,\"demo_trip_active\":true}");
}
// Feeds a simulated RMS reading (default SURGE_THRESHOLD) through the real detection path.
void handleSurgeTest() {
  float volts = server.hasArg("v") ? server.arg("v").toFloat() : SURGE_THRESHOLD;
  if (!(volts > 0.0f && volts <= 300.0f)) {
    server.send(400, "application/json", "{\"error\":\"v_must_be_0_to_300\"}");
    return;
  }
  lockShared();
  bool demo = demoTripActive, running = surgeTestActive || surgeTestRequested;
  if (!demo && !running) {
    surgeTestRequested = true;
    surgeTestVolts = volts;
  }
  float activeVolts = surgeTestVolts;
  unlockShared();
  statusDirty.store(true);
  if (demo) {
    server.send(409, "application/json", "{\"error\":\"surge_demo_active\"}");
    return;
  }
  // A repeated press while a test runs is not an error; report the test already in progress.
  char body[80];
  snprintf(body, sizeof(body), "{\"ok\":true,\"already_active\":%s,\"surge_test_v\":%.1f}",
           running ? "true" : "false", activeVolts);
  server.send(200, "application/json", body);
}
void handleTrials() {
  int count;
  lockShared();
  count = trialCount;
  unlockShared();
  JsonDocument doc;
  server.sendHeader("Content-Disposition", "attachment; filename=trials.json");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("{\"capacity\":" + String(MAX_TRIALS) + ",\"count\":" + String(count) + ",\"trials\":[");
  for (int i = 0; i < count; i++) {
    Trial t;
    lockShared();
    t = trials[i];
    unlockShared();
    doc.clear();
    trialJson(doc.to<JsonObject>(), t);
    String body;
    serializeJson(doc, body);
    if (i) server.sendContent(",");
    server.sendContent(body);
  }
  server.sendContent("]}");
  server.sendContent("");
}
void handleTrialsCsv() {
  server.sendHeader("Content-Disposition", "attachment; filename=trials.csv");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv; charset=utf-8", "");
  server.sendContent(trialCsvHeader());
  int count;
  lockShared();
  count = trialCount;
  unlockShared();
  for (int i = 0; i < count; i++) {
    Trial t;
    lockShared();
    t = trials[i];
    unlockShared();
    std::string row = trialCsvLine(t);
    server.sendContent(row.c_str());
  }
  server.sendContent("");
}
void serialCommand(const char* line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) {
    Serial.println("ERR expected JSON command");
    return;
  }
  String cmd = doc["cmd"] | "";
#if RECOVERY_TEST_HOOKS
  if (cmd == "test_main_stall") {
    Serial.println("TEST main task stall; watchdog reset expected.");
    for (;;) vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (cmd == "test_acquisition_stall" && acquisitionTaskHandle) {
    // Exercise watchdog recovery without leaving a mutex owned by a suspended task.
    lockShared();
    vTaskSuspend(acquisitionTaskHandle);
    unlockShared();
    Serial.println("TEST acquisition suspended; watchdog reset expected.");
    return;
  }
#endif
  if (cmd == "session") {
    const char* id = doc["id"] | "";
    if (activeTrial >= 0 || !*id || strlen(id) >= sizeof(sessionId)) {
      Serial.println("ERR session requires idle trial and ID 1..47 chars");
      return;
    }
    strlcpy(sessionId, id, sizeof(sessionId));
    Serial.println("OK session selected; existing trials retained");
  } else if (cmd == "start") {
    int number = doc["number"] | 0;
    const char* reference = doc["reference"] | "";
    if (!*sessionId || number < 1 || number > 65535 || !*reference || strlen(reference) >= 96) {
      Serial.println("ERR session, number and reference required");
      return;
    }
    lockShared();
    bool duplicate = false;
    for (int i = 0; i < trialCount; i++)
      if (!strcmp(trials[i].session, sessionId) && trials[i].number == number) duplicate = true;
    if (activeTrial >= 0 || trialCount >= MAX_TRIALS || duplicate) {
      unlockShared();
      Serial.println("ERR active, full or duplicate trial");
      return;
    }
    activeTrial = trialCount++;
    Trial& t = trials[activeTrial];
    t = Trial{};
    strlcpy(t.session, sessionId, sizeof(t.session));
    strlcpy(t.reference, reference, sizeof(t.reference));
    t.number = number;
    t.startUs = esp_timer_get_time();
    t.wall = clockEpoch();
    published.trialRevision++;
    unlockShared();
    Serial.println("OK trial started");
  } else if (cmd == "end") {
    const char* notes = doc["notes"] | "";
    const char* physical = doc["physical"] | "not measured";
    if (strlen(notes) >= 160 || strlen(physical) >= 96) {
      Serial.println("ERR notes max159, physical max95 chars");
      return;
    }
    lockShared();
    if (activeTrial < 0) {
      unlockShared();
      Serial.println("ERR no active trial");
      return;
    }
    Trial& t = trials[activeTrial];
    t.endUs = esp_timer_get_time();
    t.complete = true;
    strlcpy(t.notes, notes, sizeof(t.notes));
    strlcpy(t.physical, physical, sizeof(t.physical));
    strlcpy(t.state, relayName(published.p.state), sizeof(t.state));
    activeTrial = -1;
    published.trialRevision++;
    unlockShared();
    Serial.println("OK trial ended; no detection is retained as a result");
  } else if (cmd == "calibration") {
    Published p;
    lockShared();
    p = published;
    unlockShared();
    float reference = doc["reference_v"] | 0.0f;
    if (!p.measurement.valid || p.measurement.rawRms <= 0.0f || reference <= 0 || reference > 300 || !p.sampleUs || esp_timer_get_time() - p.sampleUs > 2500000ULL) {
      Serial.println("ERR valid recent sample and measured reference_v in (0,300] required");
      return;
    }
    Serial.printf("Calibration reference=%.3f V, raw_rms_adc_volts=%.6f, active scale=%.6f (CAL_DEFAULT=%.6f), displayed=%.3f V\n",
                  reference, p.measurement.rawRms, voltageCal.load(), CAL_DEFAULT, p.rms);
    Serial.printf("Proposed scale=%.6f; not applied. Apply it from the dashboard Calibration card, or edit CAL_DEFAULT in config.h.\n",
                  reference / p.measurement.rawRms);
  } else if (cmd == "dump") {
    acquisition.dumpRequested.store(true);
    Serial.println("OK raw voltage burst will print after the next acquisition");
  } else if (cmd == "clear_trials") {
    if (activeTrial >= 0 || strcmp(doc["confirmation"] | "", "EXPORTED")) {
      Serial.println("ERR idle session and confirmation EXPORTED required");
      return;
    }
    JsonDocument empty;
    empty["schema"] = 1;
    empty["trials"].to<JsonArray>();
    if (!storage.commit("/trials.json", empty, TRIAL_FILE_BYTE_CAP)) {
      Serial.println("ERR trial storage write failed; records retained in RAM");
      return;
    }
    lockShared();
    trialCount = 0;
    published.trialRevision++;
    savedTrials = published.trialRevision;
    unlockShared();
    Serial.println("OK trial records cleared; protection and event history retained");
  } else if (cmd == "format_storage") {
    // Only an unmountable filesystem may be formatted; a mounted one keeps its records.
    if (storage.mounted || strcmp(doc["confirmation"] | "", "ERASE")) {
      Serial.println("ERR storage must be unmounted and confirmation ERASE required");
      return;
    }
    Serial.println("Formatting LittleFS; load stays OFF until local reset and voltage qualification.");
    bool formatted = LittleFS.format();
    storage.writable = true;
    storage.begin();
    Serial.printf("%s storage formatted=%d mounted=%d\n", storage.mounted ? "OK" : "ERR", formatted, storage.mounted);
  } else Serial.println("ERR commands: session, start, end, calibration, clear_trials, format_storage");
}
void serviceSerial() {
  static char line[512];
  static size_t used = 0;
  static bool overflow = false;
  for (int n = 0; n < 64 && Serial.available(); n++) {
    char c = Serial.read();
    if (c == '\n') {
      if (!overflow && used) {
        line[used] = 0;
        serialCommand(line);
      } else if (overflow) Serial.println("ERR command too long");
      used = 0;
      overflow = false;
    } else if (c != '\r') {
      if (used < sizeof(line) - 1) line[used++] = c;
      else overflow = true;
    }
  }
}
void setup() {
  // Relay off (load connected) from boot; the first bad reading trips it within one burst.
  digitalWrite(RELAY_PIN, LOAD_ON_LEVEL);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LED_GREEN_PIN, OUTPUT);
  pinMode(LED_AMBER_PIN, OUTPUT);
  pinMode(LED_RED_PIN, OUTPUT);
  updateLeds(protection);
  Serial.begin(115200);
  recoveryBegin();
  if (BOOT_RELAY_SELFTEST_MS && esp_reset_reason() != ESP_RST_BROWNOUT && !recoveryCrashLoop) {
    Serial.printf("SELFTEST relay ON %lu ms (load isolated), then OFF\n", (unsigned long)BOOT_RELAY_SELFTEST_MS);
    digitalWrite(RELAY_PIN, LOAD_OFF_LEVEL);
    delay(BOOT_RELAY_SELFTEST_MS);
    digitalWrite(RELAY_PIN, LOAD_ON_LEVEL);
    Serial.println("SELFTEST relay OFF; ESP32 still running after relay switching");
  }
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  sharedMutex = xSemaphoreCreateMutex();
  if (!sharedMutex) {
    Serial.println("FAULT mutex allocation failed; OFF retained");
    return;
  }
  snprintf(bootId, sizeof(bootId), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  storage.begin();
  loadRecords();
  // Boot in Normal (relay off, load connected). Surge-only isolation: a crash loop is
  // reported (recovery_crash_loop) but does not isolate; only a restored lockout does.
  recoveryHold.store(false);
  protection.restore(protection.state == RelayState::Lockout, protection.tripCount, esp_timer_get_time());
  if (protection.state != RelayState::Lockout) protection.state = RelayState::Normal;
  published.p = protection;
  updateLeds(protection);
  taskStarted = xTaskCreate(acquisitionOwner, "acquisition", 6144, nullptr, 2, &acquisitionTaskHandle) == pdPASS;
  if (!taskStarted) {
    protection.restore(true, 3, esp_timer_get_time());
    published.p = protection;
    updateLeds(protection);
  }
  WiFi.mode(WIFI_STA);
  // The loop owns retries so automatic reconnect cannot race a manual attempt.
  WiFi.setAutoReconnect(false);
  WiFi.onEvent([](WiFiEvent_t event) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) wifiDisconnected.store(true);
    else if (event == ARDUINO_EVENT_WIFI_STA_CONNECTED || event == ARDUINO_EVENT_WIFI_STA_GOT_IP)
      wifiDisconnected.store(false);
  });
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  printNetworkStatus(WiFi.status() == WL_CONNECTED ? "connected" : "connection pending");
  configTime(GMT_OFFSET_S, DST_OFFSET_S, NTP_SERVER);
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", PAGE_HTML);
  });
  server.on("/data", HTTP_GET, handleData);
  server.on("/surges", HTTP_GET, handleSurges);
  server.on("/events", HTTP_GET, handleEvent);
  server.on("/trials", HTTP_GET, handleTrials);
  server.on("/trials.csv", HTTP_GET, handleTrialsCsv);
  server.on("/clearlogs", HTTP_POST, handleClearLogs);
  server.on("/relay-test", HTTP_POST, handleRelayTest);
  server.on("/simulate-surge", HTTP_POST, handleSimulateSurge);
  server.on("/test-surge", HTTP_POST, handleSurgeTest);
  server.on("/calibrate", HTTP_POST, handleCalibrate);
  server.on("/clearlogs", HTTP_GET, []() {
    server.send(405, "application/json", "{\"error\":\"POST_required\"}");
  });
  server.on("/favicon.ico", HTTP_GET, []() {
    server.send(204);
  });
  server.begin();
  streamServer.begin();
  Serial.println("Monitoring started. Capture disabled pending measured hardware profile. Serial accepts JSON trial commands.");
  Serial.printf("Voltage sensor: GPIO%d | Current sensor: ACS712 on GPIO%d | Sensitivity: %.0f mV/A | Scale: %.4f A/V\n", VOLTAGE_PIN, CURRENT_PIN, ACS712_SENSITIVITY_MV_PER_A, CURRENT_CAL_A_PER_V);
  Serial.println("Test commands (newline terminated):");
  Serial.println("  {\"cmd\":\"session\",\"id\":\"bench-001\"}");
  Serial.println("  {\"cmd\":\"start\",\"number\":1,\"reference\":\"normal reference\"}");
  Serial.println("  {\"cmd\":\"end\",\"notes\":\"observed result\",\"physical\":\"not measured\"}");
  Serial.println("  {\"cmd\":\"calibration\",\"reference_v\":<your meter reading, e.g. 224.0>}");
  Serial.println("  {\"cmd\":\"clear_trials\",\"confirmation\":\"EXPORTED\"}");
  Serial.println("  {\"cmd\":\"dump\"}  (prints one raw voltage burst)");
}
void loop() {
  if (!sharedMutex) {
    delay(100);
    return;
  }
  server.handleClient();
  serviceStream();
  serviceSerial();
  persistRecords();
  pruneRecords();
  serviceGraphs();
  // A stale acquisition is an application fault, independent of signal validity.
  lockShared();
  uint64_t sample = published.sampleUs;
  unlockShared();
  uint64_t completed = esp_timer_get_time();
  bool processingOK = taskStarted && sample && completed - sample < PROCESS_FRESH_US;
  // The first burst has not arrived yet during startup; that is not a processing fault.
  if ((!processingOK && completed >= PROCESS_FRESH_US) || !recoveryWatchdogOK.load()) {
    recoveryHold.store(true);
    faultOff();
  }
  uint32_t completedMs = uint32_t(completed / 1000);
  recoveryLoopMs.store(processingOK ? completedMs : 0);
  if (!processingOK) recoveryHealthySinceMs.store(0);
  else if (!recoveryHealthySinceMs.load()) recoveryHealthySinceMs.store(completedMs);
  static bool previousHold = true;
  bool held = recoveryHold.load();
  if (held != previousHold) {
    Serial.println(held ? "RECOVERY processing fault: load inhibited; local acknowledgement required."
                        : "RECOVERY local acknowledgement accepted; voltage qualification required.");
    previousHold = held;
  }
  if (processingOK && !recoveryStable && completedMs - recoveryHealthySinceMs.load() >= 120000UL) {
    recoveryStable = true;
    recoveryStreak = 0;
    recoverySaveRTC();
    Serial.println("RECOVERY application stable for 120 s; boot streak cleared.");
  }
  static uint32_t retry = millis(), offlineSince = millis();
  static bool wasConnected = false, wifiReset = false;
  uint32_t now = millis();
  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) printNetworkStatus("reconnected");
  wasConnected = connected;
  if (connected) {
    retry = offlineSince = now;
    wifiReset = false;
  } else if (now - offlineSince >= WIFI_REBOOT_MS) {
    Serial.println("WiFi offline 5 min after driver restart; restarting ESP32 (load OFF at boot).");
    faultOff();
    Serial.flush();
    ESP.restart();
  } else if (!wifiReset && now - offlineSince >= WIFI_RESET_MS) {
    wifiReset = true;
    retry = now;
    Serial.println("WiFi offline 60 s; restarting Wi-Fi driver.");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  if (!connected && now - retry >= WIFI_RETRY_MS) {
    retry = now;
    Serial.println("WiFi retry...");
    // WL_DISCONNECTED can also mean an association is still in progress.
    // Cancel it before starting again; retain the radio and saved credentials.
    // Retry only after the driver has reported a completed disconnection.
    // Otherwise cancel the unfinished attempt and wait for its event; no delay loop.
    esp_err_t result = wifiDisconnected.exchange(false) ? esp_wifi_connect() : esp_wifi_disconnect();
    if (result != ESP_OK) Serial.printf("WiFi recovery request failed: %s\n", esp_err_to_name(result));
  }
  if (acquisition.dumpReady.load()) {
    Serial.printf("DUMP n=%d burst_us=%lu\n", ADC_SAMPLES, (unsigned long)acquisition.dumpBurstUs);
    for (int i = 0; i < ADC_SAMPLES; i++) Serial.printf(i % 20 == 19 ? "%u\n" : "%u,", acquisition.dumpBuffer[i]);
    Serial.println("DUMP_END");
    acquisition.dumpReady.store(false);
  }
  static uint32_t lastVoltagePrint = 0;
  if (SERIAL_VOLTAGE_PRINT_MS && now - lastVoltagePrint >= SERIAL_VOLTAGE_PRINT_MS) {
    lastVoltagePrint = now;
    lockShared();
    Measurement m = published.measurement;
    float volts = published.rms, averageVolts = published.displayRms, amps = published.current;
    bool ampsDemo = published.currentEstimated;
    RelayState state = published.p.state;
    bool manual = relayTestActive, manualOff = relayTestOff, surgeTest = surgeTestActive;
    float testVolts = surgeTestVolts;
    unlockShared();
    // V is the latest 100 ms burst (what protection sees); V_avg is the dashboard's 1 s mean.
    // gap is the longest pause between two samples in the burst; normal is ~100 us, larger means interruptions.
    // rms_dir is the sample RMS in ADC counts that V is scaled from.
    Serial.printf(ampsDemo ? "DEMO_I=%.2f " : "I=%.2f ", amps);
    Serial.printf("V=%.1f V_avg=%.1f valid=%d reason=%s min=%u max=%u mean=%.1f pp=%u rms_dir=%.1f hz=%.2f rms_n=%u zc=%u gap=%lu burst_us=%lu relay=%s%s gpio%d=%d",
                  volts, averageVolts, m.valid, m.invalidReason(), m.minimum, m.maximum, m.mean, m.maximum - m.minimum,
                  m.directRmsCounts, m.frequencyHz, m.rmsSamples, m.crossings, (unsigned long)m.maxIntervalUs, (unsigned long)(m.endUs - m.startUs), relayName(state), manual ? (manualOff ? "/manual_off" : "/manual_on") : "",
                  RELAY_PIN, digitalRead(RELAY_PIN));
    if (surgeTest) Serial.printf(" SURGE_TEST simulated_v=%.1f", testVolts);
    Serial.println();
  }
  // A router absent for WIFI_REBOOT_MS restarts the ESP32 above. Missing acquisition deliberately stops feeding;
  // task-creation failure stays diagnostic-only with the load inhibited.
  if (processingOK || !taskStarted || completed < PROCESS_FRESH_US) {
    if (esp_task_wdt_status(nullptr) == ESP_OK && esp_task_wdt_reset() != ESP_OK) {
      recoveryWatchdogOK.store(false);
      recoveryHold.store(true);
      faultOff();
    }
  }
  delay(2);
}
