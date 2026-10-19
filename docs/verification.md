# Verification record

Software checks performed September 12–13, 2026 on the development PC. None of the fixture inputs below are real experimental trials.

| Check | Result |
| --- | --- |
| Original generic ESP32 baseline | PASS, 1,067,645 bytes flash (81%); 56,048 bytes static RAM (17%) |
| Production protection host tests | PASS: startup qualification, 245 V boundary, 207 V boundary, invalid/NaN sensing, sustained undervoltage, interruption/gap handling, duplicate observations, trip expiry, latched lockout, debounced reset |
| Production storage/record host tests | PASS with in-memory filesystem: short write, byte cap, interrupted rename, backup recovery, corrupt-file quarantine, mount failure, malformed state and pre-ON reservation |
| Trial serializer tests | PASS: complete no detection, incomplete session, copied first-event metadata, null unsynchronized clock |
| CSV/JSON export comparison | PASS using production serializers: 25 columns, event ID/value/response, clock nullability, quotes/newlines, spreadsheet formula prefix escaping |
| JavaScript parse and headless Edge | PASS, no page errors |
| Alert behavior | PASS: historical first visit, wait/checking persistence, local dismissal, reload, later event, multiple-event reconnect, retention gap |
| Browser freshness/polling | PASS: stale values and command status cleared, alert retained offline, sequential polling requests |
| Responsive rendering | PASS at 320, 375, 600 and 768 px, device scale factor 2; no viewport overflow; repeated canvas redraw does not grow backing height |
| Screenshot inspection | 320 and 768 px screenshots visually inspected; generated screenshots in `build/dashboard-*.png` |
| Connected board discovery | No detected ports |

The initial integration build exceeded the ESP32 static DRAM segment. Duplicate snapshots were removed, record output changed to streaming, and trial capacity bounded to 32. Subsequent integration builds passed. The final generic-target build result is recorded below after recompilation.

## Evidence and reproducibility

- Host command: `tests\run_host.cmd` (MSVC 14.51.36231, production headers, ArduinoJson 7.4.3, Python CSV comparison).
- Browser command: `node tests/dashboard_test.cjs` (Playwright, installed headless Edge, localhost fixtures).
- Firmware: bundled Arduino CLI 1.5.1, `--fqbn esp32:esp32:esp32`, core 3.3.11, ArduinoJson 7.4.3. Build cache reused at `%TEMP%\relay-audit-baseline`; the source baseline itself is preserved separately.
- Source snapshot SHA-256: `baseline/relay.ino` = `D6F0A68CDD9B0AF1BC85D01A3CD5678A4312CEAA9B0BC069A8BED4878EC6E157`.
- Dashboard snapshot SHA-256: `baseline/webpage.h` = `1900DC5917D968B543ADC1B6AD322EE2ECFCD36D53E04106FF548A4212AD87EC`.
- Unmodified binary `ebpage.h` SHA-256: `105192781870DC57DB29652EB49243864A1538232218B1E3CC99BD3FABED846D`.

## Pending hardware-dependent acceptance

Actual-board build/profile, upload, real ADC/sensor calibration, bandwidth and sampling characterization, continuous pre/post waveform capture, timing-error/overflow acceptance, real Wi-Fi/flash stress on acquisition, runtime heap/stack endurance, watchdog/brownout behavior, filesystem power-cut testing on flash, GPIO/driver/LED verification, contact opening measurements, all physical inspection items and the 30 real trials remain pending. The host filesystem model does not certify LittleFS power-loss behavior on the target flash.

Only command timing is implemented. Capture stays compile-time disabled and peak/duration remain unavailable. See `hardware-and-validation.md` and `capture-profile.example.json` for the evidence required to proceed.

## September 27, 2026: saved alert history

The earlier checks above describe the earlier dashboard. The refreshed suite now verifies the current two-tab UI.

- Production host tests pass for 244.99 V, 245.00 V and 245.01 V, with the third qualifying reading triggering protection; existing filter, recovery and lockout tests also pass.
- Graph host tests pass for the four-slot queue and overflow, ring wrapping, correct event association, restart-style storage reload, missing values, estimated current, legacy records, short writes, corrupt files, mount failure, retention/clear cleanup and deletion failure.
- Current browser fixtures pass for saved Alerts graphs, immutable graphs during live refresh, JSON exports, legacy fallback, refresh, clearing, relocated controls and 320/375/600/768 px layouts at device scale 2. No JavaScript errors or viewport overflow. The 320 px screenshot was visually inspected.
- `tests\run_host.cmd` and `node tests/dashboard_test.cjs` are the reproducible commands. Fixtures are not hardware measurements.
- Final `esp32:esp32:esp32` compile passes: 1,095,969 / 1,310,720 bytes flash (83%); 121,188 / 327,680 bytes static RAM (36%), leaving 206,492 bytes for runtime allocations and stacks. Runtime minimum heap/stack has not been measured on the board. No board upload was performed.

## October 5, 2026: YCCH6 timed trip and recovery

- `tests\run_host.cmd` passes: 244.99/245.00/245.01 V, no trip before five elapsed seconds, median-filter threshold timing, interrupted high voltage, invalid/NaN sensing, missing-measurement gaps, inclusive 220/235 V recovery boundaries, out-of-band recovery resets, 30-second startup and recovery, sustained-voltage faults, three-trip lockout, trip expiry, restored state and local reset.
- Storage/record and CSV/JSON export host suites pass, including durable pre-ON trip reservation. These are simulated software checks, not physical measurements.
- Qualification retains the existing 2.5-second maximum measurement gap. Storage readiness can delay physical reconnection beyond the 30-second qualification.
- Hardware contact validation remains pending: with the coil supply disconnected, verify HIGH opens COM-NO and LOW closes it before any powered coil test. No upload or powered coil test was performed.
- ESP32 compilation passes (`esp32:esp32:esp32`): 1,095,941 / 1,310,720 bytes flash (83%); 121,236 / 327,680 bytes static RAM (36%). Build directory: `%TEMP%\relay-current-build`.
