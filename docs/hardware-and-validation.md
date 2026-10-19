# Hardware evidence and validation procedure

Every physical item below is pending. Software compilation and simulated inputs do not establish electrical safety or physical isolation performance.

## Release gates

Record exact ESP32 module/board, flash/PSRAM, deployed partition map and firmware settings. Confirm the sensor is the stated ZMPT101B and the isolator/driver identity and contact arrangement; source comments are not physical evidence. Establish whether sensing is upstream of isolation: a downstream sensor may prevent automatic recovery while OFF.

| Signal | Preserved configuration | Required evidence |
| --- | --- | --- |
| Voltage | GPIO34, 12-bit ADC, 11 dB attenuation | Actual ADC mapping, conditioned output limits/bias, transfer curve, isolation and bandwidth |
| Relay command | GPIO26, HIGH OFF / LOW ON | Full driver/coil/contact truth table, loss-of-power and pre-boot behavior |
| Reset | GPIO25 INPUT_PULLUP | Wiring, release/press polarity, bounce and access |
| Green/amber/red | GPIO13/14/27, active high | Availability, resistors/drivers, polarity and exactly one physical indicator |
| Diagnostic marker | Unassigned, disabled | Confirmed spare low-voltage pin and approved probing reference before implementation |

No mains conductor, SPD/contactor mains terminal or unisolated waveform may connect directly to GPIO/ADC. Confirm the appropriate isolation/conditioning and driver interfaces before electrical testing. Protective earth and ESP32 signal ground are separate design concerns.

## Capture characterization gate

Capture remains disabled and no waveform chart is advertised. Complete `capture-profile.example.json` with measured evidence before implementing/enabling the continuous acquisition ring and pre/post capture slots. No continuous ADC mode, rate or new GPIO is selected by this change.

1. Use an isolated/conditioned low-voltage input and independent calibrated reference. Validate RMS scale, flat/disconnected input, noise, DC-bias steps and clipping. Collect multiple stable samples. The serial calibration command proposes a multiplier only; it never normalizes mains automatically or changes the live threshold.
2. Record burst duration, within-burst maximum interval, inter-burst gaps and maximum gap from `/data`. Run offline, unsynchronized, Wi-Fi reconnecting, under multiple/slow HTTP clients, and while state/event/trial storage is busy. Record heap minimum, largest allocation and acquisition stack high-water. These burst diagnostics do not measure full interval distributions or sensor bandwidth; use independent instrumentation to establish jitter/bandwidth and detection limits.
3. Establish target sample rate, required pre/post durations, allowed jitter/gap, sample/capture/queue capacities, RAM/flash byte budgets, overflow policy and sustained test duration together. Validate a single ADC owner against the actual board/core and partition layout.
4. Implement the approved capture profile with immutable completed capture slots, negative/zero/positive trigger-relative timestamps, explicit gaps/overflow/incomplete flags and bounded selected-capture persistence. Keep units as ADC counts until instantaneous-voltage conversion is independently validated.
5. The later event chart must label zero as **RMS detection**, not disturbance onset, use `relative_time_us / 1000` for milliseconds, and leave gaps open. Capture timing, peak and duration remain null until measured.

## Serial trial workflow

Use USB serial at 115200 baud, newline-terminated JSON (maximum 511 bytes per command). Select a session, then start a trial **before** applying its approved condition. Reference/notes/physical fields are operator observations, never generated measurements.

```json
{"cmd":"session","id":"validation-001"}
{"cmd":"start","number":1,"reference":"normal reference; independently measured value recorded here"}
{"cmd":"end","notes":"observed dashboard/LED behavior","physical":"not measured"}
```

The trial records first detection metadata, total detection count, measured sample count/invalid count, first/last sample times, maximum observed inter-burst gap, final commanded state and operator observations. A completed trial without an event is `no_detection`. Check sample coverage before interpreting no detection as a successful normal trial or a missed protection response. Interrupted sessions remain `incomplete` after reboot and cannot be silently resumed. Additional detections are counted; only the first event's full metadata is copied into the durable trial record. Export rolling events too when multiple-event detail is required.

Export both `/trials` and `/trials.csv`, compare records, and archive them before starting a new batch. The 32 slots never overwrite trials automatically. Once exported, an idle session may be cleared explicitly:

```json
{"cmd":"clear_trials","confirmation":"EXPORTED"}
```

This does not clear lockout or event history. CSV strings are quoted/escaped, with a protective apostrophe for potential spreadsheet-formula prefixes. JSON preserves original operator text.

For calibration only, with a valid recent sample and an independently measured reference:

```json
{"cmd":"calibration","reference_v":230.0}
```

This prints the current raw ADC RMS, displayed voltage and a proposed `CAL_DEFAULT`. Verify the reference and transfer over the required range before manually updating configuration; 230 here is an example input, not a measured value. After editing, `/data` should show `sensorOk: true`, nonzero stable `raw_rms_adc_volts`, `adc_min`/`adc_max` away from 0/4095, and a voltage close to the independent meter after filtering settles.

## Thirty real trials: pending

Perform six repetitions in each group, for a mixed set of 30 actual trials. Fixture tests are not part of this denominator. For each, record expected condition, independent applied reference, detection result, event ID/value/threshold and software command timing if present, commanded state, actual physical result only when measured, acquisition coverage and notes.

| Group | Trials | Expected software behavior to verify |
| --- | --- | --- |
| Normal reference | 1–6 | Qualified normal indication and no unexpected event |
| Below-trip warning excursion | 7–12 | Valid abnormal reading before confirmation, amber while still commanded ON; choose a condition consistent with distinct retained thresholds |
| At/above 245 V RMS equivalent | 13–18 | RMS event and OFF command; no physical timing inferred from firmware |
| Sustained undervoltage | 19–24 | Confirmation after 60 elapsed seconds, interruptions reset qualification |
| Invalid sensing and qualified recovery | 25–30 | OFF/FAULT, recovery interruptions, wait/stability sequence, persistent alert |

Account for the 5-second RMS trip qualification, 30-second safe-voltage recovery interval and three-episode lockout in scheduling. Do not bypass these protections to speed up trials. Start with conditioned low-voltage simulation. Any mains work needs the approved setup and qualified supervision; do not create uncontrolled mains surges.

## Physical response and reboot tests

Measure conditioned reference, detection marker (only after a spare pin is approved), output command and actual contact opening separately with approved equipment/probing. Firmware `response_us` excludes acquisition latency and does not prove contact motion. Never assume a scope ground clip can connect to an arbitrary reference; do not defeat oscilloscope PE.

Test boot, reset, watchdog, brownout and power loss in every state, especially before/after pre-ON durable reservation and before/after OFF-state persistence. Verify missing/corrupt state, full/unmounted filesystem and repeated reboots. The conservative reservation can increase restored trip count and require a physical reset; it must never restore ON without qualification. Inspect the selected core watchdog configuration and record actual reset causes; no new watchdog policy was enabled without measurement.

## Physical inspection checklist

- [ ] Metal enclosure, applicable PSU/EMI filter/SPD PE connections and installation earth path verified.
- [ ] Grounding lugs/terminals and rail/PE-block bonding inspected with appropriate equipment.
- [ ] DIN rail, terminal blocks, end caps/plates and fittings secure and appropriate.
- [ ] Conductors, ferrules/lugs and terminals match; no exposed strands.
- [ ] LINE/LOAD orientation and panel/device orientation match approved drawings and markings.
- [ ] Exposed connections appropriately insulated; wiring supported and strain-relieved.
- [ ] AC wiring segregated from low-voltage circuitry using suitable routing/support.
- [ ] ESP32 securely mounted; low-voltage connections insulated; hot glue not primary mounting/insulation.
- [ ] Sensor isolation/conditioning and driver stages physically verified.
- [ ] LED pins, resistors, polarity and physical state mapping verified.
- [ ] Reset, pre-boot, loss-of-power and brownout contact behavior measured.
- [ ] Protective earth and signal ground treated according to the verified design.
- [ ] Scope/probe ratings, references and isolation arrangement approved before measurement.

Record inspector, date, instrument, method and evidence for each completed item. None is satisfied by this software change.

Before a powered coil test, disconnect the coil supply and check COM-NO continuity: GPIO26 HIGH must open the contact (coil OFF), and LOW must close it (coil ON). Then validate timed automatic trips and recovery with conditioned input. Physical contact validation is pending.
