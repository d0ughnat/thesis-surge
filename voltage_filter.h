#pragma once
#include <cmath>
#include "config.h"

class VoltageFilter {
  float samples[3] = {};
  unsigned count = 0, next = 0;
 public:
  float rms = 0, display = 0;
  void update(bool valid, float value) {
    if (!valid || !std::isfinite(value)) {
      count = next = 0; rms = display = 0; return;
    }
    samples[next] = value;
    next = (next + 1) % 3;
    if (count < 3) ++count;
    // Seed immediately; reject isolated outliers once the window is full.
    if (count == 1) rms = display = value;
    if (count == 3) {
      float a = samples[0], b = samples[1], c = samples[2];
      rms = a > b ? (b > c ? b : (a > c ? c : a))
                  : (a > c ? a : (b > c ? c : b));
    }
    display += VOLTAGE_FILTER_ALPHA * (rms - display);
  }
};
