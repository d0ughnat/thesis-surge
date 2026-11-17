# Sensor Testing: ZMPT101B, SCT-013-000, ACS712

Read [electrical-safety.md](electrical-safety.md) first. Never present a calculated "expected" value as a measured one.

## Diagnostic chain

Every analog sensor fault lives at one point in this chain. Find the first point where the signal stops behaving as expected:

```
stimulus → sensor element → module output (V) → ADC pin (V) → ADC counts
        → offset removal → RMS/average → filter → calibration scale → clamp/threshold → display/log
```

Measure from the left. Do not tune calibration until the raw counts are proven correct.

## Universal checklist (every analog sensor)

- [ ] Exact variant confirmed (label/datasheet), supply voltage measured at the module.
- [ ] Output range at that supply fits 0–3.3 V at the ADC pin (with margin; ADC is non-linear above ~3.1 V).
- [ ] Pin is ADC1 (GPIO32–39); pin in firmware matches wiring.
- [ ] No-stimulus reading: mean, peak-to-peak, noise recorded.
- [ ] Stimulus reading: amplitude changes in proportion to the stimulus.
- [ ] No clipping: min > ~50 counts and max < ~4045 counts.
- [ ] Sampling: known sample interval, window covers a whole number of mains cycles (or ≥10 cycles). Mains frequency (50 or 60 Hz) confirmed — do not assume.
- [ ] Firmware: offset is measured dynamically (mean of the window) or validated; RMS is computed on offset-removed samples; filters/clamps/thresholds don't hide changes.

## Standalone waveform/RMS test sketch

Use for all three sensors (one at a time). Reads one ADC1 pin at a fixed interval and prints raw statistics. Drives no outputs, no Wi-Fi.

```cpp
// hw-tests/adc_waveform/adc_waveform.ino
// Raw AC/DC waveform statistics for one analog sensor. Send 'w' to dump one window of raw samples.
#include <Arduino.h>

const int PIN = 34;               // VERIFY: wiring and firmware pin map; must be ADC1 (GPIO32-39)
const float MAINS_HZ = 50.0f;     // VERIFY: local mains frequency (50 or 60)
const int CYCLES = 10;            // whole cycles per window
const uint32_t SAMPLE_US = 200;   // 5 kS/s; analogRead takes ~10-20 us so this is achievable
const int MAX_N = 4000;
const int CLIP_LO = 10, CLIP_HI = 4085;

uint16_t buf[MAX_N];
int n = 0;

void capture() {
  uint32_t next = micros();
  for (int i = 0; i < n; i++) {
    while ((int32_t)(micros() - next) < 0) {}
    buf[i] = analogRead(PIN);
    next += SAMPLE_US;
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  analogReadResolution(12);
  n = (int)(CYCLES * 1000000.0f / MAINS_HZ / SAMPLE_US);
  if (n > MAX_N) n = MAX_N;
  Serial.printf("cfg pin=%d mains_hz=%.1f cycles=%d sample_us=%lu n=%d\n",
                PIN, MAINS_HZ, CYCLES, (unsigned long)SAMPLE_US, n);
}

void loop() {
  uint32_t t0 = micros();
  capture();
  uint32_t elapsed = micros() - t0;

  uint16_t mn = 4095, mx = 0;
  uint32_t sum = 0, clipped = 0;
  for (int i = 0; i < n; i++) {
    uint16_t v = buf[i];
    if (v < mn) mn = v;
    if (v > mx) mx = v;
    if (v <= CLIP_LO || v >= CLIP_HI) clipped++;
    sum += v;
  }
  float mean = (float)sum / n;
  double sq = 0;
  for (int i = 0; i < n; i++) { float c = buf[i] - mean; sq += (double)c * c; }
  float rms = sqrt(sq / n);
  uint32_t meanMv = analogReadMilliVolts(PIN);  // calibrated single read near the DC offset

  Serial.printf("min=%u max=%u pp=%u mean=%.1f rms_counts=%.2f clipped=%lu dc_mv=%lu window_us=%lu\n",
                mn, mx, mx - mn, mean, rms, (unsigned long)clipped,
                (unsigned long)meanMv, (unsigned long)elapsed);

  if (Serial.available() && Serial.read() == 'w') {
    for (int i = 0; i < n; i++) Serial.println(buf[i]);
    Serial.println("end");
  }
  delay(1000);
}
```

Interpretation:
- `window_us` should be ≈ `n * SAMPLE_US`; if much longer, sampling is being delayed.
- `rms_counts` is AC RMS with DC removed. Converting to volts at the pin: `rms_counts * 3.3 / 4095` (approximate; ADC gain is not perfectly linear).
- For a sinusoid, `pp ≈ 2.83 * rms_counts`. A much smaller ratio (flat-topped) means clipping/saturation; a much larger ratio means spikes/noise.
- `clipped > 0` at any setting is a FAIL for that setting.

## ZMPT101B AC voltage sensor

**What it is:** a small 2 mA:2 mA current transformer. The common blue module adds a series primary resistor, a burden, and an op-amp (often LM358) with a gain trimpot; output is an AC waveform riding on a DC bias of about VCC/2.

**Checks:**
- Module supply (often 5 V or 3.3 V; verify the module). At 5 V the bias is ~2.5 V and the output swing can exceed 3.3 V → must not feed the ESP32 directly without verifying the swing. At 3.3 V the bias is ~1.65 V, but the LM358 output cannot reach the top rail (~1.5 V headroom), so the positive half clips early.
- Zero-input (primary disconnected): mean ≈ bias, `pp` small (noise only).
- Trimpot sets gain. Too high → clipping (RMS stops rising). Too low → signal buried in noise.
- Isolation: not safety-rated. See electrical-safety.md.

**Variac sweep test (qualified person operates the variac and reference meter):**
1. Variac at 0, everything enclosed, sketch running.
2. Step to e.g. 50, 100, 150, 200, 230 V (and 245 V only if the setup is rated). At each step the qualified person reports the reference meter RMS; you record `pp`, `rms_counts`, `clipped`.
3. Expected: `rms_counts` proportional to reference voltage (ratio constant within a few %), `clipped=0` at the highest test voltage, mean roughly constant.
4. Linearity check: compute `k = V_ref / rms_counts` at each step. Spread under ~3 % → PASS.

**Calibration:** `V_rms = rms_counts * k`, with `k` from a reference reading near the operating point. Verify at a second voltage. Never set `k` so the display reads "230 V" without an independent meter.

### Why the reported voltage stays constant when the variac changes

Work through this in order; each item names the evidence that confirms it.

| # | Cause | Evidence |
|---|---|---|
| 1 | Output saturated/clipping (gain too high, 5 V-biased output above 3.3 V, LM358 rail limit) | `clipped>0`, `max` at ~4095 or flat tops in `w` dump; `pp` stops rising |
| 2 | Wrong pin / sensor not connected / module unpowered | `pp` at noise level regardless of variac; mean at 0, 4095, or floating |
| 3 | ADC2 pin with Wi-Fi active | Pin is GPIO 0/2/4/12–15/25–27 and firmware uses Wi-Fi |
| 4 | Firmware reports a fixed/nominal/default value (fallback when "invalid", clamp, constant written on startup, demo/test mode) | Raw `rms_counts` from the test sketch changes but app value doesn't; search firmware for nominal constants, fallbacks, demo flags |
| 5 | Filter far too slow (tiny IIR alpha, long averaging) or update only on threshold crossing | Value drifts slowly after a step; check filter coefficient and update cadence |
| 6 | Offset handled wrong: fixed offset subtracted, or RMS computed without removing DC (DC dominates, so the AC change is a small fraction) | Firmware computes RMS on raw counts or uses hard-coded midpoint |
| 7 | Validity gate rejects every window, last good value held | Firmware's invalid-reason output (e.g. clipping, too few crossings, drift) |
| 8 | Window not covering whole cycles / too few samples → jittery rather than constant, but can look stuck after filtering | `window_us` vs cycle period |
| 9 | Display threshold (e.g. readings below a minimum shown as 0 or as a fixed value) | Check display/minimum constants |
| 10 | Variac output actually not changing at the measurement point | Reference meter at the ZMPT101B input |

## SCT-013-000 current transformer

**What it is:** split-core CT, 100 A : 50 mA (ratio 2000:1), **current output, no internal burden**. Voltage-output variants (SCT-013-005/010/.../100 with "1 V" rating) have built-in burdens — verify the label.

**Open-secondary hazard:** connect the burden before clamping onto a live conductor. See electrical-safety.md.

**Interface circuit (3.3 V ADC):**
- Burden resistor across the CT secondary.
- Bias the burden's low side to mid-supply with two equal resistors (e.g. 10 kΩ–100 kΩ) from 3.3 V to GND, with a 10 µF capacitor from midpoint to GND.
- CT/burden output to ADC1 pin (optionally through ~1 kΩ series resistor).

**Burden sizing:** peak swing available ≈ 1.65 V minus margin (use ~1.5 V for ESP32 non-linearity).

```
I_sec_peak = I_primary_max_rms * 1.414 / 2000
R_burden   = 1.5 V / I_sec_peak
```
Example (calculation, not a measurement): 100 A max → I_sec_peak = 70.7 mA → R ≈ 21 Ω → use 22 Ω (or smaller). For a lower max current, a larger burden gives better resolution: 20 A max → 14.1 mA peak → ~100 Ω.

**Scaling:** `I_rms = rms_volts_at_pin * 2000 / R_burden` (theoretical). Then trim against a clamp meter reading.

**Tests:**
1. No current: mean ≈ midpoint counts (~2048 ± bias tolerance), `pp` noise only. If mean is 0 or 4095 → bias network missing/broken.
2. Known low-voltage AC load or several turns through the CT (N turns multiply current by N) with a clamp meter reference.
3. Proportionality across 2–3 currents; `clipped=0` at max expected current.
4. Low-current floor: report the noise floor (no-load RMS in amps after scaling); readings below ~2–3× that floor are not meaningful.

**Typical faults:** no burden (reads tiny/erratic, hazardous), wrong variant (double burden), clamp not fully closed (low reading), clamped around both live and neutral (reads ~0), bias cap missing (noisy offset), DC offset subtraction fixed instead of dynamic.

## ACS712 Hall current sensor

**What it is:** Hall-effect sensor with ratiometric output: `Vout = VCC/2 + sensitivity * I`. Datasheet supply **4.5–5.5 V** (5 V nominal).

| Variant | Sensitivity at 5 V |
|---|---|
| ACS712ELCTR-05B | 185 mV/A |
| ACS712ELCTR-20A | 100 mV/A |
| ACS712ELCTR-30A | 66 mV/A |

**ESP32 compatibility:**
- At 5 V supply: zero point ≈ 2.5 V, output can swing ~0.5–4.5 V → **exceeds 3.3 V ADC limit**. Use a divider (e.g. 10 kΩ top / 20 kΩ bottom gives ×0.667: 2.5 V → 1.67 V), and scale sensitivity by the same factor. Check divider loading.
- Powered at 3.3 V: outside the datasheet supply range. It may appear to work, but zero offset, sensitivity and noise are not guaranteed. Flag as a hypothesis-level risk; recommend 5 V + divider. Do not "correct" for it by guessing a sensitivity.
- Output is ratiometric: VCC noise appears directly on the output. Measure VCC at the module.

**Tests:**
1. Zero current: record mean (counts and `dc_mv`), noise `pp`. Expected zero ≈ VCC/2 × divider ratio. Compute zero-current noise in amps: `rms_counts * (3.3/4095) / (sensitivity_V_per_A * divider)`.
2. Known DC current from a current-limited bench supply into a power resistor (low voltage). Read reference current from the supply/DMM. Expected mean shift = `I * sensitivity * divider`.
3. Reverse the current (bidirectional variants): shift reverses sign symmetrically.
4. AC: use RMS of offset-removed samples, as for the CT.

**Calibration:** measure zero offset at boot or on command with no current flowing (and document that requirement); sensitivity from a known current. Two-point verification.

**Typical faults:** wrong variant sensitivity in firmware, 5 V output clipping the ESP32 ADC, out-of-spec 3.3 V supply, fixed zero offset that drifts with temperature/supply, noise floor (tens of mA to ~0.2 A) mistaken for real current, mains wiring through a module whose PCB isn't rated for it.

## Sensor result template

```
Sensor: <model/variant>   Pin: GPIO<n> (ADC1)   Supply measured: <V> at module
Zero-stimulus: mean=<> pp=<> rms_counts=<> clipped=<>          [measured | not measured]
Stimulus steps (reference instrument: <>):
  ref=<value>  rms_counts=<>  pp=<>  clipped=<>  k=<ref/rms>
Linearity spread: <%>   Clipping at max: <yes/no>
Firmware value vs reference: <>
Status: PASS | FAIL | NOT TESTED  — evidence: <>
```
