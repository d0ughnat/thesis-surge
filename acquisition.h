#pragma once
#include <Arduino.h>
#include <esp_timer.h>
#include "config.h"
struct Measurement {
  uint64_t startUs=0, endUs=0, gapUs=0;
  uint32_t maxIntervalUs=0;
  uint16_t minimum=0, maximum=0, crossings=0;
  float rawRms=0, mean=0, meanDrift=0;
  bool valid=false;
  const char* invalidReason() const {
    if (minimum==0 || maximum>=ADC_RESOLUTION-1) return "ADC_CLIPPED";
    if (maximum-minimum<SENSOR_MIN_PP_COUNTS) return "SIGNAL_TOO_SMALL";
    if (crossings==0) return "NO_AC_CROSSINGS";
    if (crossings>SENSOR_MAX_ZCROSS) return "EXCESS_NOISE_CROSSINGS";
    if (meanDrift>SENSOR_MAX_MEAN_DRIFT) return "BIAS_DRIFT";
    return valid ? "NONE" : "NO_VALID_SAMPLE";
  }
  uint16_t currentMinimum=0, currentMaximum=0, currentCrossings=0;
  float currentRawRms=0, currentMean=0;
  bool currentValid=false;
};
// This is the sole ADC owner. Bursts are characterized, not continuous capture.
class Acquisition {
  uint16_t samples[ADC_SAMPLES], currentSamples[ADC_SAMPLES];
  float previousMean=-1;
  uint64_t previousEnd=0;
 public:
  Measurement read() {
    Measurement m; m.startUs=esp_timer_get_time();
    m.gapUs=previousEnd ? m.startUs-previousEnd : 0;
    uint32_t sum=0, currentSum=0; uint64_t last=m.startUs;
    m.minimum=ADC_RESOLUTION-1;
    for (int i=0;i<ADC_SAMPLES;i++) {
      samples[i]=analogRead(VOLTAGE_PIN);
      currentSamples[i]=analogRead(CURRENT_PIN);
      uint64_t now=esp_timer_get_time();
      uint32_t interval=now-last; last=now;
      if (interval>m.maxIntervalUs) m.maxIntervalUs=interval;
      sum+=samples[i]; currentSum+=currentSamples[i];
      if (samples[i]<m.minimum) m.minimum=samples[i];
      if (samples[i]>m.maximum) m.maximum=samples[i];
    }
    m.endUs=esp_timer_get_time(); previousEnd=m.endUs;
    m.mean=float(sum)/ADC_SAMPLES; m.currentMean=float(currentSum)/ADC_SAMPLES;
    double square=0; int previousSign=0;
    for (int i=0;i<ADC_SAMPLES;i++) {
      float centered=samples[i]-m.mean;
      int sign=centered>SENSOR_ZC_DEADBAND ? 1 : centered < -SENSOR_ZC_DEADBAND ? -1 : 0;
      if (sign && previousSign && sign!=previousSign) m.crossings++;
      if (sign) previousSign=sign;
      square+=double(centered)*centered;
    }
    float drift=previousMean<0 ? 0 : fabsf(m.mean-previousMean); previousMean=m.mean;
    m.meanDrift=drift;
    m.valid=m.maximum-m.minimum >= SENSOR_MIN_PP_COUNTS && m.crossings>0 && m.crossings<=SENSOR_MAX_ZCROSS &&
      drift<=SENSOR_MAX_MEAN_DRIFT && m.minimum>0 && m.maximum<ADC_RESOLUTION-1;
    m.rawRms=sqrt(square/ADC_SAMPLES)*(VREF_MV/1000.0f/ADC_RESOLUTION);
    double currentSquare=0; uint16_t currentMin=ADC_RESOLUTION-1,currentMax=0; int previousCurrentSign=0;
    for(int i=0;i<ADC_SAMPLES;i++) {
      if(currentSamples[i]<currentMin) currentMin=currentSamples[i];
      if(currentSamples[i]>currentMax) currentMax=currentSamples[i];
      float centered=float(currentSamples[i])-m.currentMean;
      int sign=centered>SENSOR_ZC_DEADBAND ? 1 : centered < -SENSOR_ZC_DEADBAND ? -1 : 0;
      if(sign && previousCurrentSign && sign!=previousCurrentSign) m.currentCrossings++;
      if(sign) previousCurrentSign=sign;
      currentSquare+=double(centered)*centered;
    }
    m.currentMinimum=currentMin; m.currentMaximum=currentMax;
    m.currentRawRms=sqrt(currentSquare/ADC_SAMPLES)*(VREF_MV/1000.0f/ADC_RESOLUTION);
    bool currentBiasOk=m.currentMean>=CURRENT_SENSOR_MIN_MEAN_COUNTS && m.currentMean<=CURRENT_SENSOR_MAX_MEAN_COUNTS;
    bool currentNotClipped=currentMin>CURRENT_SENSOR_MIN_ADC_COUNTS && currentMax<CURRENT_SENSOR_MAX_ADC_COUNTS;
    float currentAmps=m.currentRawRms*CURRENT_CAL_A_PER_V;
    // Current does not need mains-style zero crossings, but it must be centered
    // like an ACS712 and must not hit the ESP32 ADC rails.
    m.currentValid=currentBiasOk && currentNotClipped && currentAmps<=CURRENT_MAX_VALID_A;
    return m;
  }
};
