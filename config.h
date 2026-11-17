#pragma once
// Bench-only fault injection; keep disabled in deployed firmware.
#ifndef RECOVERY_TEST_HOOKS
#define RECOVERY_TEST_HOOKS 0
#endif
#define WIFI_RETRY_MS   10000UL
// Escalating Wi-Fi recovery: restart the Wi-Fi driver, then the whole ESP32 (boots with load OFF).
#define WIFI_RESET_MS   60000UL
#define WIFI_REBOOT_MS  300000UL
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET_S    28800L
#define DST_OFFSET_S    0
#define VOLTAGE_PIN     34
#define CURRENT_PIN     35
#define ACS712_SENSITIVITY_MV_PER_A 100.0f // ACS712T-20A; use 185 for 5 A or 66 for 30 A variants.
#define CURRENT_CAL_A_PER_V (1000.0f / ACS712_SENSITIVITY_MV_PER_A)
#define RELAY_PIN       26
#define RESET_PIN       25
// Active-HIGH relay module (no click with IN LOW on the bench), COM-NO wired to the
// normally closed YCCH6-02 coil (2NC).
// Load ON: IN LOW, relay off, coil de-energized (contacts closed).
// Load OFF / surge: IN HIGH, relay on, coil energized (contacts open).
#define LOAD_ON_LEVEL 0
#define LOAD_OFF_LEVEL 1
// The relay only switches the YCCH6 contactor coil. Sensors are wired separately
// from the relay contact path, so relay state must not be used to create readings.
#define RELAY_POWERS_SENSORS false
// Status LEDs (DRAFT — confirm free on your board before relying on them)
#define LED_GREEN_PIN   13
#define LED_AMBER_PIN   14
#define LED_RED_PIN     27
#define LED_ACTIVE_HIGH true
// ~206 samples per mains cycle measured on this board, so 2000 covers ~9 whole cycles per
// burst (1000 gave 3-4 cycles and about +/-1.6% burst-to-burst jitter).
#define ADC_SAMPLES     2000
#define VOLTAGE_SAMPLE_PERIOD_US 100000ULL
// The dashboard's "1 s average" refreshes at this period; protection and the live push use every burst.
#define DISPLAY_UPDATE_PERIOD_US 1000000ULL
// Live dashboard push (Server-Sent Events) on its own port; every burst is sent as it is measured.
#define STREAM_PORT 81
// Browsers that can watch at once; each gets the same frames. The ESP32 core allows 16 sockets
// in total and the web server, stream listener and NTP need some of them.
#define STREAM_MAX_CLIENTS 8
#define STREAM_REQUEST_WAIT_MS 50UL
#define STREAM_STATUS_PERIOD_MS 1000UL
// Bench testing: print each latest reading on serial at this period; 0 disables.
#define SERIAL_VOLTAGE_PRINT_MS 1000UL
#define VREF_MV         3300.0f
#define ADC_RESOLUTION  4096
// ZMPT101B RMS scale used until the dashboard Calibration card saves another one.
// Calibrate only from an independent meter reading; do not force readings to nominal mains voltage.
// 2026-10-27: 1736.1 read a mean of 236.4 V (11 bursts) against a 224 V meter reading -> 1645.2.
// 2026-10-27: 1645.2 (2000-sample bursts) read ~3% high against a 224 V meter -> 1645.2 / 1.03.
#define CAL_DEFAULT     1597.3f
// Dashboard calibration: the typed meter reading must be above MIN and up to MAX (0 V itself
// gives no signal to scale), and the resulting scale within CAL_MAX_ADJUST_PCT of CAL_DEFAULT
// so a typing mistake cannot blind the surge trip.
#define CAL_REFERENCE_MIN_V 0.0f
#define CAL_REFERENCE_MAX_V 250.0f
#define CAL_MAX_ADJUST_PCT  25.0f
#define CAL_PATH "/calibration.json"
// Change detection: a change starts when CHANGE_CONFIRM_BURSTS consecutive readings are away
// from the steady level by CHANGE_MIN_V or CHANGE_NOISE_FACTOR times the steady scatter,
// whichever is larger. It ends when the last CHANGE_WINDOW_BURSTS valid bursts (~2 s) have been
// flat for CHANGE_SETTLE_BURSTS readings, and their mean becomes the new steady level. Rate of
// change is the trend over the same window.
#define CHANGE_WINDOW_BURSTS 10
#define CHANGE_MIN_V         1.0f
#define CHANGE_NOISE_FACTOR  3.0f
#define CHANGE_CONFIRM_BURSTS 2
#define CHANGE_SETTLE_BURSTS 5
#define DISPLAY_VOLTAGE_MIN_RMS 20.0f
// Confirmed RMS overvoltage threshold; not a fast-transient detector.
#define SURGE_THRESHOLD 245.0f
// A surge must persist this long (2 consecutive ~200 ms readings) before tripping, so one
// reading disturbed by load switching does not isolate. 0 = trip on a single reading.
#define SURGE_CONFIRM_MS 200
// Readings in the first moments after boot are ignored while the sensor settles.
#define STARTUP_SETTLE_MS 3000ULL
// Surge-only isolation: sustained under/overvoltage and an invalid sensor reading are
// reported as warnings but keep the load connected. Set true to isolate on them too.
#define TRIP_ON_ABNORMAL_VOLTAGE false
#define TRIP_ON_INVALID_SENSOR   false
// Undervoltage isolation: a reading in this band for UNDERVOLTAGE_CONFIRM_MS turns the load OFF.
// Below the band (supply loss or sensor fault territory) does not trip.
#define UNDERVOLTAGE_TRIP_MIN_RMS 100.0f
#define UNDERVOLTAGE_TRIP_MAX_RMS 207.0f
// Same confirmation as a surge (2 consecutive ~200 ms readings).
#define UNDERVOLTAGE_CONFIRM_MS   SURGE_CONFIRM_MS
// After a trip, reconnect once readings stay in this band (below SURGE_THRESHOLD).
// SAFE_V_MIN sits above UNDERVOLTAGE_TRIP_MAX_RMS so a sag cannot reconnect and trip in a loop.
#define SAFE_V_MIN      212.0f
#define SAFE_V_MAX      240.0f
// User-configured normal RMS operating band; its top stays below SURGE_THRESHOLD.
// Normal band is NOMINAL_VOLTAGE_RMS (meter-measured source, 2026-10-27) +/- NORMAL_BAND_PCT: 212.8..235.2 V.
#define NOMINAL_VOLTAGE_RMS    224.0f
#define NORMAL_BAND_PCT        5.0f
#define NORMAL_VOLTAGE_MIN_RMS (NOMINAL_VOLTAGE_RMS * (1.0f - NORMAL_BAND_PCT / 100.0f))
#define NORMAL_VOLTAGE_MAX_RMS (NOMINAL_VOLTAGE_RMS * (1.0f + NORMAL_BAND_PCT / 100.0f))
static_assert(NORMAL_VOLTAGE_MAX_RMS < SURGE_THRESHOLD, "Normal band must stay below the surge threshold");
static_assert(SAFE_V_MIN > UNDERVOLTAGE_TRIP_MAX_RMS, "Reconnect band must start above the undervoltage trip band");
// Boot self-test: relay ON (load isolated) this long at power-on so the click proves the
// relay works. Skipped after a brownout or crash loop; 0 disables.
#define BOOT_RELAY_SELFTEST_MS 0UL
#define ABNORMAL_CONFIRM_S  60
#define RECOVERY_CONFIRM_S  10
#define UNDERVOLTAGE_TRIP_ENABLED true  // false = WARNING-only on brownout
// Sensor-disconnect detection (ADC counts)
#define SENSOR_MIN_PP_COUNTS 20
#define SENSOR_MAX_ZCROSS    60
#define SENSOR_ZC_DEADBAND   4
#define SENSOR_MAX_MEAN_DRIFT 150
// This ACS712T-20A is powered from 3.3 V, so its zero-current output should
// sit near half of the ESP32 ADC range. These limits reject floating, clipped,
// or incorrectly biased inputs before they become fake amp readings.
#define CURRENT_SENSOR_MIN_MEAN_COUNTS 1500
#define CURRENT_SENSOR_MAX_MEAN_COUNTS 2600
#define CURRENT_SENSOR_MIN_ADC_COUNTS 10
#define CURRENT_SENSOR_MAX_ADC_COUNTS 4085
#define CURRENT_NOISE_FLOOR_A 0.25f
#define CURRENT_MAX_VALID_A 20.0f
// true: when the ACS712 reading is invalid, show a SIMULATED current (1.0-2.5 A from voltage),
// labelled "DEMO LOAD FROM VOLTAGE" on the dashboard, dashed in history and DEMO_I on serial.
// Never used by protection. Set false to show only measured current.
#define DEMO_CURRENT_FROM_VOLTAGE true
#define CURRENT_SENSOR_MIN_PP_COUNTS 8
#define CURRENT_SENSOR_MAX_ZCROSS 120
#define CURRENT_SENSOR_MAX_MEAN_DRIFT 150
// After a trip, the load reconnects once voltage stays in SAFE_V_MIN..SAFE_V_MAX this long.
#define RECONNECT_CONFIRM_S 10
#define TRIP_WINDOW_MS     1800000UL
#define SURGE_COOLDOWN_MS  10000UL
#define DEBOUNCE_MS        50UL
#define DEMO_TRIP_DURATION_MS 10000UL
#define SURGE_PATH "/surges.json"
#define RELAY_PATH "/relay.json"
#define MAX_SURGE_ENTRIES 150
#define SURGE_RETAIN_DAYS 30
#define HISTORY_SIZE 60


// No capture profile has been validated on hardware.
#define CAPTURE_ENABLED false
static_assert(!CAPTURE_ENABLED, "Supply and validate an acquisition/capture profile before enabling capture");
#define EVENT_FILE_BYTE_CAP 98304
#define TRIAL_FILE_BYTE_CAP 65536
#define MAX_TRIALS 32
