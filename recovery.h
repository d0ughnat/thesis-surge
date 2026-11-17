#pragma once
#include <Arduino.h>
#include <atomic>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>

// RTC retention avoids flash writes during a reset storm. Not a lifetime counter:
// power removal/brownout may erase retention; every boot still requires acknowledgement.
RTC_NOINIT_ATTR uint32_t recoveryMagic;
RTC_NOINIT_ATTR uint32_t recoveryBoots;
RTC_NOINIT_ATTR uint32_t recoveryStreak;
RTC_NOINIT_ATTR uint32_t recoveryCheck;
static constexpr uint32_t RECOVERY_MAGIC = 0x52454332;
static constexpr uint32_t WATCHDOG_MS = 30000;
static constexpr uint64_t PROCESS_FRESH_US = 2500000ULL;
std::atomic<bool> recoveryHold{true};
std::atomic<bool> recoveryWatchdogOK{false};
std::atomic<uint32_t> recoveryLoopMs{0};
std::atomic<uint32_t> recoveryHealthySinceMs{0};
bool recoveryCrashLoop = false;
bool recoveryStable = false;

inline const char* resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWER_ON";
    case ESP_RST_EXT: return "EXTERNAL_RESET";
    case ESP_RST_SW: return "SOFTWARE_RESET";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INTERRUPT_WATCHDOG";
    case ESP_RST_TASK_WDT: return "TASK_WATCHDOG";
    case ESP_RST_WDT: return "OTHER_WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO_RESET";
    default: return "UNKNOWN_OR_OTHER";
  }
}
inline void recoverySaveRTC() {
  recoveryCheck = RECOVERY_MAGIC ^ recoveryBoots ^ recoveryStreak;
  recoveryMagic = RECOVERY_MAGIC;
}
inline bool recoverySubscribe() {
  return esp_task_wdt_status(nullptr) == ESP_OK || esp_task_wdt_add(nullptr) == ESP_OK;
}
inline void recoveryBegin() {
  if (esp_reset_reason() == ESP_RST_POWERON || recoveryMagic != RECOVERY_MAGIC ||
      recoveryCheck != (RECOVERY_MAGIC ^ recoveryBoots ^ recoveryStreak)) {
    recoveryBoots = 0;
    recoveryStreak = 0;
  }
  if (recoveryBoots != UINT32_MAX) ++recoveryBoots;
  if (recoveryStreak != UINT32_MAX) ++recoveryStreak;
  recoverySaveRTC();
  recoveryCrashLoop = recoveryStreak >= 3;
  Serial.printf("BOOT reset=%s (%d), retained_boots=%lu, unstable_boots=%lu\n",
                resetReasonName(esp_reset_reason()), int(esp_reset_reason()),
                (unsigned long)recoveryBoots, (unsigned long)recoveryStreak);
  if (recoveryCrashLoop) Serial.println("RECOVERY repeated boots detected (reported only; load stays connected).");
  Serial.println("RECOVERY boot: relay off, load connected; protection trips on surge.");
  // Do not touch brownout registers or disable an existing system watchdog.
#if ESP_IDF_VERSION_MAJOR >= 5
  esp_task_wdt_config_t cfg = {};
  cfg.timeout_ms = WATCHDOG_MS;
  cfg.idle_core_mask = (1U << portNUM_PROCESSORS) - 1U;
  cfg.trigger_panic = true;
  esp_err_t result = esp_task_wdt_init(&cfg);
  if (result == ESP_ERR_INVALID_STATE) result = esp_task_wdt_reconfigure(&cfg);
#else
  esp_err_t result = esp_task_wdt_init(WATCHDOG_MS / 1000, true);
#endif
  recoveryWatchdogOK.store(result == ESP_OK && recoverySubscribe());
  Serial.printf("RECOVERY watchdog=%s, timeout_ms=%lu, IDF=%s\n",
                recoveryWatchdogOK.load() ? "armed" : "FAILED_LOAD_INHIBITED",
                (unsigned long)WATCHDOG_MS, esp_get_idf_version());
}
inline bool recoveryCanAcknowledge(uint64_t) {
  uint32_t loop = recoveryLoopMs.load(), healthy = recoveryHealthySinceMs.load();
  uint32_t current = uint32_t(esp_timer_get_time() / 1000);
  return recoveryWatchdogOK.load() && loop && healthy && current - loop < PROCESS_FRESH_US / 1000 &&
         current - healthy >= (recoveryCrashLoop ? 60000UL : 10000UL);
}
