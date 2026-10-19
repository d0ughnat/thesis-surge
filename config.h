#pragma once
#define WIFI_RETRY_MS   10000UL
#define NTP_SERVER      "pool.ntp.org"
#define GMT_OFFSET_S    28800L
#define DST_OFFSET_S    0
#define VOLTAGE_PIN     34
#define CURRENT_PIN     35
#define ACS712_SENSITIVITY_MV_PER_A 100.0f // ACS712T-20A; use 185 for 5 A or 66 for 30 A variants.
#define CURRENT_CAL_A_PER_V (1000.0f / ACS712_SENSITIVITY_MV_PER_A)
#define RELAY_PIN       26
#define RESET_PIN       25
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
#define ADC_SAMPLES     1000
#define VOLTAGE_SAMPLE_PERIOD_US 100000ULL
#define VREF_MV         3300.0f
#define ADC_RESOLUTION  4096
// ZMPT101B RMS scale. Update only from an independent meter reading using the
// serial calibration command; do not force readings to nominal mains voltage.
#define CAL_DEFAULT     1736.1f
#define DISPLAY_VOLTAGE_MIN_RMS 20.0f
#define VOLTAGE_FILTER_ALPHA 0.10f
// Confirmed RMS overvoltage threshold; not a fast-transient detector.
#define SURGE_THRESHOLD 245.0f
#define SURGE_CONFIRM_S 5
#define SAFE_V_MIN      220.0f
#define SAFE_V_MAX      235.0f
// User-configured normal RMS operating band.
#define NOMINAL_VOLTAGE_RMS    230.0f
#define NORMAL_VOLTAGE_MIN_RMS 220.0f
#define NORMAL_VOLTAGE_MAX_RMS 235.0f
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
#define CURRENT_SENSOR_MIN_PP_COUNTS 8
#define CURRENT_SENSOR_MAX_ZCROSS 120
#define CURRENT_SENSOR_MAX_MEAN_DRIFT 150
#define RECONNECT_CONFIRM_S 30
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
