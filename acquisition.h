#pragma once
#include <Arduino.h>
#include <esp_timer.h>
#include <atomic>
#include "config.h"
struct Measurement {
  uint64_t startUs=0, endUs=0, gapUs=0;
  uint32_t maxIntervalUs=0;
  uint16_t minimum=0, maximum=0, crossings=0, rmsSamples=0;
  float rawRms=0, mean=0, meanDrift=0;
  // Sample RMS in ADC counts over whole mains cycles (scaled to volts in rawRms).
  float directRmsCounts=0;
  // Mains frequency from the whole-cycle window; 0 when fewer than two rising crossings were seen.
  float frequencyHz=0;
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
  // Heap-allocated on first read: two static buffers of ADC_SAMPLES overflow DRAM .bss.
  uint16_t *samples=nullptr, *currentSamples=nullptr;
  float previousMean=-1;
  uint64_t previousEnd=0;
 public:
  // Serial "dump": the next burst's raw voltage samples are copied here for the loop task to print.
  std::atomic<bool> dumpRequested{false}, dumpReady{false};
  uint16_t* dumpBuffer=nullptr;
  uint32_t dumpBurstUs=0;
  Measurement read() {
    Measurement m; m.startUs=esp_timer_get_time();
    if (!samples) samples=(uint16_t*)malloc(ADC_SAMPLES*sizeof(uint16_t));
    if (!currentSamples) currentSamples=(uint16_t*)malloc(ADC_SAMPLES*sizeof(uint16_t));
    if (!samples || !currentSamples) { m.endUs=esp_timer_get_time(); return m; }  // invalid: NO_VALID_SAMPLE
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
    // crossings (small deadband) feeds the noise check; cycle edges use a deadband of 10% of
    // peak-to-peak so noise near zero cannot add a false rise and break the whole-cycle window.
    float cycleDeadband=fmaxf(SENSOR_ZC_DEADBAND, 0.1f*(m.maximum-m.minimum));
    int previousSign=0, cyclePreviousSign=0, firstRise=-1, lastRise=-1, rises=0;
    for (int i=0;i<ADC_SAMPLES;i++) {
      float centered=samples[i]-m.mean;
      int sign=centered>SENSOR_ZC_DEADBAND ? 1 : centered < -SENSOR_ZC_DEADBAND ? -1 : 0;
      if (sign && previousSign && sign!=previousSign) m.crossings++;
      if (sign) previousSign=sign;
      int cycleSign=centered>cycleDeadband ? 1 : centered < -cycleDeadband ? -1 : 0;
      if (cycleSign>0 && cyclePreviousSign<0) { if (firstRise<0) firstRise=i; lastRise=i; rises++; }
      if (cycleSign) cyclePreviousSign=cycleSign;
    }
    // RMS over whole mains cycles only (rising crossing to rising crossing); a partial
    // cycle shifts the mean and RMS from burst to burst. Falls back to the full burst.
    int begin=0, end=ADC_SAMPLES;
    if (firstRise>=0 && lastRise>firstRise) { begin=firstRise; end=lastRise; }
    m.rmsSamples=end-begin;
    double windowSum=0;
    for (int i=begin;i<end;i++) windowSum+=samples[i];
    double windowMean=windowSum/m.rmsSamples, square=0;
    for (int i=begin;i<end;i++) { double centered=samples[i]-windowMean; square+=centered*centered; }
    m.directRmsCounts=sqrt(square/m.rmsSamples);
    // rises-1 whole cycles span lastRise-firstRise samples of an evenly paced burst.
    if (rises>=2 && m.endUs>m.startUs)
      m.frequencyHz=float(double(rises-1)*ADC_SAMPLES*1000000.0/(double(lastRise-firstRise)*double(m.endUs-m.startUs)));
    float drift=previousMean<0 ? 0 : fabsf(m.mean-previousMean); previousMean=m.mean;
    m.meanDrift=drift;
    m.valid=m.maximum-m.minimum >= SENSOR_MIN_PP_COUNTS && m.crossings>0 && m.crossings<=SENSOR_MAX_ZCROSS &&
      drift<=SENSOR_MAX_MEAN_DRIFT && m.minimum>0 && m.maximum<ADC_RESOLUTION-1;
    m.rawRms=m.directRmsCounts*(VREF_MV/1000.0f/ADC_RESOLUTION);
    if (dumpRequested.load() && !dumpReady.load() && (dumpBuffer || (dumpBuffer=(uint16_t*)malloc(ADC_SAMPLES*sizeof(uint16_t))))) {
      memcpy(dumpBuffer, samples, ADC_SAMPLES*sizeof(uint16_t));
      dumpBurstUs=m.endUs-m.startUs;
      dumpRequested.store(false);
      dumpReady.store(true);
    }
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
