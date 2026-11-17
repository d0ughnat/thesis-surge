#pragma once
#include <stdint.h>
#include <cmath>
#include "config.h"

enum class RelayState : uint8_t { Normal, Waiting, Checking, Lockout };
// UndervoltageTrip is appended so stored event type values stay unchanged.
enum class EventType : uint8_t { None, Surge, Overvoltage, Undervoltage, InvalidSensor, Recovery, Reset, UndervoltageTrip };
inline const char* eventName(EventType t) {
  switch (t) {
    case EventType::Surge: return "SURGE";
    case EventType::UndervoltageTrip: return "UNDERVOLTAGE_TRIP";
    case EventType::Overvoltage: return "OVERVOLTAGE";
    case EventType::Undervoltage: return "UNDERVOLTAGE";
    case EventType::InvalidSensor: return "INVALID_SENSOR";
    case EventType::Recovery: return "RECOVERY";
    case EventType::Reset: return "SYSTEM_RESET";
    default: return "NONE";
  }
}
inline bool isProtection(EventType t) { return t == EventType::Surge || t == EventType::UndervoltageTrip; }
inline const char* relayName(RelayState s) {
  switch (s) {
    case RelayState::Normal: return "normal";
    case RelayState::Checking: return "checking";
    case RelayState::Lockout: return "lockout";
    default: return "waiting";
  }
}
struct Qualification {
  uint64_t start = 0, last = 0;
  bool active = false;
  bool update(bool condition, uint64_t now, uint64_t duration) {
    if (!condition) { active = false; return false; }
    // A missed measurement cannot count as demonstrated stability.
    if (!active || now - last > 2500000ULL) { start = now; active = true; }
    last = now;
    return now - start >= duration;
  }
};
struct Decision { EventType event = EventType::None; bool commandOff = false, commandOn = false; };
class Protection {
 public:
  RelayState state = RelayState::Waiting;
  bool valid = false, abnormal = false;
  float voltage = 0;
  uint8_t tripCount = 0;
  uint64_t trips[3] = {}, waitStart = 0;
  Qualification low, high, recovery, stable, surge, under;
  bool surgeActive = false, underActive = false, invalidActive = false;
  uint64_t lastSurge = 0;
  bool haveSurge = false;

  void restore(bool lockout, uint8_t count, uint64_t now) {
    state = lockout || count>=3 ? RelayState::Lockout : RelayState::Waiting;
    tripCount = count > 3 ? 3 : count;
    for (int i=0;i<tripCount;i++) trips[i]=now;
    waitStart = now;
  }
  void reset(uint64_t now) {
    if (state != RelayState::Lockout) return;
    state = RelayState::Waiting; tripCount = 0; waitStart = now;
    stable.active = false; surge.active = false; surgeActive = false; under.active = false; underActive = false;
  }
  const char* classification() const {
    if (!valid) return "INVALID";
    if (voltage < NORMAL_VOLTAGE_MIN_RMS) return "LOW";
    if (voltage > NORMAL_VOLTAGE_MAX_RMS) return "HIGH";
    return "NORMAL";
  }
  const char* systemState() const {
    if (state != RelayState::Normal || !valid || abnormal) return "FAULT";
    return voltage < NORMAL_VOLTAGE_MIN_RMS || voltage > NORMAL_VOLTAGE_MAX_RMS ? "WARNING" : "NORMAL";
  }
  const char* reason() const {
    if (state == RelayState::Lockout) return "LOCKOUT_LOCAL_RESET_REQUIRED";
    if (!valid) return "INVALID_SENSING";
    if (state == RelayState::Waiting) return "ISOLATION_WAIT";
    if (state == RelayState::Checking) return "ISOLATION_STABILITY_CHECK";
    if (abnormal) return "SUSTAINED_VOLTAGE_FAULT";
    return classification();
  }
  Decision step(bool sensorValid, float rms, uint64_t now) {
    Decision d;
    valid = sensorValid && std::isfinite(rms) && rms>=0; voltage = rms;
    bool confirmedSurge = surge.update(valid && rms >= SURGE_THRESHOLD, now, SURGE_CONFIRM_MS*1000ULL);
    bool confirmedUnder = under.update(valid && rms >= UNDERVOLTAGE_TRIP_MIN_RMS && rms <= UNDERVOLTAGE_TRIP_MAX_RMS,
                                       now, UNDERVOLTAGE_CONFIRM_MS*1000ULL);
    bool lowDone = low.update(valid && rms < NORMAL_VOLTAGE_MIN_RMS, now, ABNORMAL_CONFIRM_S*1000000ULL);
    bool highDone = high.update(valid && rms > NORMAL_VOLTAGE_MAX_RMS, now, ABNORMAL_CONFIRM_S*1000000ULL);
    bool recovered = recovery.update(valid && rms >= NORMAL_VOLTAGE_MIN_RMS && rms <= NORMAL_VOLTAGE_MAX_RMS,
                                      now, RECOVERY_CONFIRM_S*1000000ULL);
    if (!valid) {
      if (!invalidActive || state == RelayState::Normal) d.event = EventType::InvalidSensor;
      invalidActive = true; surgeActive = false; underActive = false;
    } else {
      invalidActive = false;
      // Cooldown suppresses duplicate observations, never a qualified ON->OFF trip.
      if (confirmedSurge && (state == RelayState::Normal ||
          (!surgeActive && (!haveSurge || now-lastSurge >= SURGE_COOLDOWN_MS*1000ULL)))) {
        d.event=EventType::Surge; lastSurge=now; haveSurge=true;
      }
      surgeActive=confirmedSurge;
      // Logged once per isolation; while isolated a continuing sag just holds the load OFF.
      if (confirmedUnder && state == RelayState::Normal && d.event == EventType::None) d.event=EventType::UndervoltageTrip;
      underActive=confirmedUnder;
      if ((lowDone || highDone) && !abnormal) {
        abnormal=true;
        if (d.event == EventType::None) d.event=lowDone ? EventType::Undervoltage : EventType::Overvoltage;
      }
      if (abnormal && recovered) { abnormal=false; if (d.event==EventType::None) d.event=EventType::Recovery; }
    }
    bool mustOff = (!valid && TRIP_ON_INVALID_SENSOR) || confirmedSurge || confirmedUnder ||
                   (TRIP_ON_ABNORMAL_VOLTAGE && abnormal && (UNDERVOLTAGE_TRIP_ENABLED || rms > NORMAL_VOLTAGE_MAX_RMS));
    if (mustOff) {
      d.commandOff = true;
      if (state == RelayState::Normal) {
        int kept=0;
        for (int i=0;i<tripCount;i++) if (now-trips[i] < TRIP_WINDOW_MS*1000ULL) trips[kept++]=trips[i];
        tripCount=uint8_t(kept);
        if (tripCount<3) trips[tripCount++]=now;
        state=tripCount>=3 ? RelayState::Lockout : RelayState::Waiting;
        waitStart=now;
      } else if (state==RelayState::Checking) { state=RelayState::Waiting; waitStart=now; }
    }
    if (state == RelayState::Waiting || state == RelayState::Checking) {
      bool safe = valid && rms >= SAFE_V_MIN && rms <= SAFE_V_MAX;
      // Qualify safe voltage concurrently with clearing the sustained-fault latch.
      bool ready = stable.update(safe, now, RECONNECT_CONFIRM_S*1000000ULL);
      state = safe ? RelayState::Checking : RelayState::Waiting;
      if (ready && (!abnormal || !TRIP_ON_ABNORMAL_VOLTAGE)) { state=RelayState::Normal; d.commandOn=true; }
    } else { stable.active=false; }
    return d;
  }
};

struct ResetButton {
  bool raw = true, stable = true, armed = false;
  uint64_t changed = 0;
  bool update(bool released, uint64_t now) {
    if (released != raw) { raw=released; changed=now; }
    if (now-changed < DEBOUNCE_MS*1000ULL) return false;
    if (released) armed=true;
    if (stable == released) return false;
    stable=released;
    if (!released && armed) { armed=false; return true; }
    return false;
  }
};
