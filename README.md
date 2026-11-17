# AC Power Monitor

Automatic fault recovery and the changed startup acknowledgement procedure are
documented in [RECOVERY.md](RECOVERY.md). Every boot now holds the load OFF until
a deliberate local acknowledgement and the existing safe-voltage qualification.

ESP32 Arduino RMS voltage monitoring, isolation commands, event history and a local mobile dashboard. The corrected software is a prototype for validation; it does not establish contact opening time, fast-transient detection, or electrical installation compliance.

## Implementation status

Implemented: early OFF command, isolation on invalid sensing, elapsed protection/recovery timers, latched lockout, a debounced local reset, acquisition in its own task, event identity and command timestamps, persistent browser alerts, bounded recoverable storage, timestamped history, serial trial sessions and JSON/CSV export. Surge protection commands OFF on the first valid RMS reading at or above 245 V, without median filtering or a confirmation delay. GPIO assignments are retained.

**Waveform capture is deliberately disabled.** No board-specific sampling rate, sensor bandwidth, pre/post buffer profile or timing tolerance has been measured. `config.h` rejects enabling capture without further implementation and validation. `/events` explicitly returns no waveform, peak or duration. The acquisition diagnostics characterize the existing 1,000-read bursts; they are not continuous sampling. The plan's continuous capture and millisecond waveform graph phases remain pending that evidence.

No device has been flashed or formatted. `baseline/` contains the original readable sources.

## Build

Verified software toolchain: Arduino CLI 1.5.1, ESP32 core 3.3.11, ArduinoJson 7.4.3. The generic `esp32:esp32:esp32` target is an audit configuration, not confirmation of the connected module or deployed partition layout.

The existing Wi-Fi configuration was moved into ignored `secrets.h`. For a fresh checkout, copy `secrets.example.h` to `secrets.h` and enter local credentials. An empty `CLEAR_TOKEN` disables event deletion; configure a private token to enable the administrative POST endpoint. Do not commit local secrets or the original-source snapshot, which also contains the original credentials.

```powershell
& 'C:\Users\PC\AppData\Local\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp32:esp32:esp32 --build-path "$env:TEMP\relay-integration" .
```

Confirm the actual board, flash size, partition scheme and driver polarity before selecting an upload target. Do not create or upload a filesystem image until existing data has been exported and the partition map checked. Startup uses `LittleFS.begin(false)` and does not mount SPIFFS or format either filesystem. A legacy SPIFFS filesystem requires explicit export with the old firmware and a separately verified import/migration process.

## Modules and ownership

| File | Responsibility |
| --- | --- |
| `config.h` | Existing pins/thresholds, software buffer limits and disabled capture gate |
| `protection.h` | Pure elapsed-time state machine and physical reset debounce |
| `acquisition.h` | Sole ADC owner, burst measurement and timing/quality diagnostics |
| `events.h` | Stable boot/sequence IDs, measurement/command metadata, JSON representation |
| `alert_history.h` | Four-slot snapshot queue, bounded graph files, integrity checks and saved-history JSON |
| `storage.h` | Bounded loads, checked writes, temporary file and backup commits |
| `relay_persistence.h` | Validated state schema and conservative pre-ON trip reservation |
| `trials.h` | Recorded trial and first-detection metadata, including no detection |
| `relay.ino` | Initialization, acquisition/protection task, RAM publication, deferred persistence, REST and serial interfaces |
| `webpage.h` | Offline assets, responsive dashboard, live push stream client, calibration card, event notification and exports |

The acquisition task performs ADC reads and protection decisions. An OFF command precedes event publication, mutex acquisition, serialization and filesystem operations. The main loop owns HTTP, JSON, filesystem and serial administration. A mutex protects short copies and bounded in-memory event/trial operations; no network or flash work holds it. No application ADC ISR or competing ADC reader is installed. Flash operations can still stall parts of the ESP32 platform; task separation is not a hard real-time guarantee. Measure rate, gaps and command timing under load.

## Protection semantics

- With COM-NO wiring and an active-low relay module, the normally closed YCCH6-02 uses GPIO26 HIGH for load ON (coil OFF, contactor contacts closed) and LOW for load OFF (coil ON, contactor contacts open). ON/OFF in the dashboard and protection logic refer to the load. Firmware does not control pre-boot GPIO behavior; loss of coil power closes the normally closed contacts.
- Startup holds OFF until 30 continuous seconds of valid 220-235 V readings. Corrupt/unavailable state starts in lockout; local reset starts the same qualification, never immediate ON.
- Invalid sensing commands OFF, reports invalid voltage as `null`, and clears smoothing immediately. ADC quality checks still require conditioned-input validation.
- The first valid RMS reading at or above 245 V commands OFF. There is no added confirmation delay or median filter in the protection path. Detection still requires an RMS acquisition burst (nominally 100 ms cadence); this is not fast transient protection. Cooldown suppresses duplicate observations only.
- Excursions below 220 V or above 235 V still confirm a sustained fault after 60 seconds. Ten seconds in that band clears the abnormal latch, concurrently with reconnect qualification. A measurement gap over the existing 2.5-second freshness limit resets qualification timers.
- Reconnection requires 30 continuous seconds within inclusive 220-235 V and a successful durable trip reservation. Leaving the band or invalid sensing resets qualification. Storage persistence/retry latency may delay the actual ON command beyond 30 seconds. Manual ON is allowed only when protection is normal; manual OFF and the 10-second surge demo can hold the load OFF.
- Only actual ON-to-OFF episodes increment the three-trip, 30-minute window. Repeated detections while already OFF do not increment it. Successful reconnection does not erase the window. LOCKOUT never expires automatically.
- A press must follow a debounced release. Holding the reset button cannot reset a later lockout.
- FAULT covers every commanded shutdown, invalid sensing and abnormal latch. Valid pre-confirmation excursions while ON are WARNING. Red has priority for lockout or confirmed surge; otherwise green indicates valid 220-235 V and amber indicates valid voltage above 235 V. Amber stays OFF at 0 V, low voltage or invalid sensing. Network, clock and storage health are separate fields.

Before ON is permitted, state storage reserves one possible future trip. If power fails before the later OFF-state write, boot restores the conservative reservation. Trip ages restart on reboot because monotonic clocks are not comparable between boots. This can cause earlier lockout after repeated reboots; it avoids silently discarding an uncommitted trip. Hardware reset/brownout tests remain required.

## API

| Method and path | Contents |
| --- | --- |
| `GET /data` | Voltage quality, classification/state/reason, sample timestamp/age, latest event, clock/storage/task/heap/ADC diagnostics |
| `GET http://DEVICE:81/` | Server-Sent Events push stream: `history` once on connect, `live` for every measured burst (about 4-5 per second), `status` every second and at once on a state change |
| `POST /calibrate` | Form-encoded `reference_v` (meter reading, above 0 up to 250 V) or `reset=1`; no token check |
| `GET /surges` | Up to 150 events, newest first, including type, ID, clock quality and command timing |
| `GET /surges?since=ID` | Retained entries newer than ID; `retention_gap` if the cursor is missing |
| `GET /surges?since=ID&all=1` | Complete retained table plus cursor-gap indication |
| `GET /events?id=ID` | Event detail and saved `history`; raw waveform `samples` stays empty |
| `GET /trials`, `GET /trials.csv` | Recorded trial exports |
| `POST /clearlogs` | Form-encoded private `token`; affects event history only |
| `GET /clearlogs` | 405; never deletes anything |

The dashboard provides a single ON/OFF relay button; its label shows the next action and the text below shows the commanded state. The local `POST /relay-test` endpoint accepts `off=0` to hold the load ON or `off=1` to hold it OFF. The API retains `auto=1` to resume automatic protection. A real surge cancels manual control and commands OFF. Manual ON requests are rejected with HTTP 409 while automatic protection is waiting, checking, or locked out. Manual ON is also blocked at the GPIO command whenever protection is outside its normal state. Otherwise manual control lasts until automatic control resumes, a restart occurs, or a surge demo starts. This endpoint has no token check. Administrative HTTP is intended for a controlled local network; no HTTPS or user authentication system was added.

For ZMPT101B scale calibration, measure the supply with an independent meter and type that reading (above 0, up to 250 V) into the dashboard's Voltage Calibration card. The firmware divides it by the mean raw RMS of the last ten bursts (about 2 s), stores the scale in `/calibration.json` and applies it from the next burst to both the displayed voltage and the protection thresholds' input. A scale more than 25% away from `CAL_DEFAULT` is rejected. RESET TO BUILT-IN deletes the stored scale. Thresholds themselves (`SURGE_THRESHOLD`, the normal band) are not changed by calibration.

The USB serial command still reports a proposed scale without applying it:

```json
{"cmd":"calibration","reference_v":230.0}
```

`/data` exposes `raw_rms_adc_volts`, `voltage_cal`, `voltage_cal_custom`, `voltage_cal_default`, `voltage_scale_applied`, `adc_min`, `adc_max` and `sensorOk` for verification.

The dashboard reports a voltage change when two consecutive bursts are away from the steady level by at least 1.0 V or three times the steady reading scatter, whichever is larger (`CHANGE_*` in `config.h`). The change stays open while the voltage keeps moving, for example during a variac sweep, and closes when the last ten bursts have been flat for five readings; their mean becomes the new steady level. `dvdt` is the least-squares trend over the same ten bursts (about 2 s), not the difference of two bursts. Both are display aids only and never command the relay. Invalid sensing still reports `voltage: null`; do not hard-code nominal mains voltage.

`sample_us`, `trigger_us` and `command_us` are 64-bit monotonic microseconds within `boot_id`. `timestamp_us` is retained as a compatibility alias for sample time, replacing the old request-time meaning. Wall-clock epoch is nullable and never retroactively invented for unsynchronized events. `response_us` measures decision-entry to output-command completion, not ADC acquisition latency, physical disturbance onset or contact response. Current and apparent power are unavailable, not zero measurements.

The banner is tied to event IDs, remains visible during wait/checking, and supports View/Dismiss. Dismissal stays in that browser's local storage and never modifies protection or history. A new event reopens the alert. The first visit shows the latest retained protection event as historical. The dashboard no longer polls: the ESP32 pushes each reading over the port 81 stream, the browser reconnects by itself, and three seconds without a frame is shown as disconnected. A frame that cannot be queued without blocking drops that browser's stream rather than delaying the main loop. Alert latency includes one RMS burst (about 0.2 s) and network time.

## Storage and limits

- 150 event records, 60 RAM history readings and **32 trial slots** (enough for the specified 30 trials). Trials never roll off automatically; full/duplicate/active-trial starts are rejected.
- Event/trial files are capped at 98,304/65,536 serialized bytes. Large exports and persistence writes stream bounded records instead of allocating one large output document. Boot loads are capped before parsing with a nesting limit. Runtime minimum heap, largest allocation and task stack high-water must still be measured on the actual board.
- Changed state/events/trials are serviced at most every five seconds, with state first. HTTP handling can delay persistence, but not the application-owned acquisition loop. An unexpected power loss may lose pending event/trial records; the pre-ON reservation separately protects trip accounting.
- Each commit checks serialized length and file size, writes `.tmp`, and keeps the prior committed `.bak` until replacement succeeds. A corrupt primary is quarantined as `.corrupt`; if that quarantine already exists, writes are blocked instead of overwriting evidence. Storage failure indication is sticky for the current boot.
- Valid dated events are pruned every minute after 30 days. Unsynchronized events are only removed by the count cap or explicit deletion. The latest alert also remains in RAM after rolling-history pruning/deletion until reboot. Trials retain their first event metadata independently of rolling history.
- Clear operations replace the live file; a prior backup/corrupt diagnostic file can remain. They are not secure erase operations. Do not change the partition scheme or erase flash to resolve an ordinary storage error.

See [hardware and validation](docs/hardware-and-validation.md) for release gates and trial procedures, and [verification results](docs/verification.md) for checks actually performed.

## Tests

`tests\run_host.cmd` uses the installed Visual Studio Build Tools and ArduinoJson headers to exercise the production protection code and storage serializer, with an in-memory filesystem that injects write/rename failures. Adjust its tool paths for another machine.

```powershell
.\tests\run_host.cmd
npm.cmd --prefix build install --no-save playwright
node tests/dashboard_test.cjs
```

The browser test uses installed headless Edge and a localhost fixture server. It checks behavior and 320/375/768 px layouts at device scale 2, and writes screenshots under ignored `build/`. Fixture voltages/events are software tests, never experimental records.

## Saved alert graphs (September 27, 2026)

Alerts includes the selected alert details, voltage (V RMS) and current (A) graphs, and JSON export. Relay Test and Surge Demo are on Now; Recorded Event navigation has been removed. Saved graph timestamps are relative to the event trigger, and live refresh does not replace a selected snapshot.

Every protection alert snapshots up to 60 readings ending at detection (about six seconds at 100 ms cadence), including invalid readings as gaps. Estimated current retains a per-reading `current_estimated` flag. These are RMS history readings, not raw waveform capture; capture remains disabled.

`/events?id=ID` includes `history.status` (`available`, `pending`, or `unavailable`) and `history.samples` with `sample_us`, nullable `ts`, nullable `voltage`/`current`, and `current_estimated`. Older records without saved history show Graph unavailable. The event JSON export contains this same object.

The acquisition task only copies to a four-slot RAM queue after issuing relay commands. Loop-owned writes create separate `/graph-EVENT_ID.bin` files with a version and integrity checksum. Each is bounded by `sizeof(AlertHistory)+4` (1,980 bytes for the current ESP32 layout); 150 files use about 297 KB before filesystem overhead. Queue overflow or failed writes leave the alert intact with unavailable graphs. Successful committed snapshots reload after restart. A power loss before deferred alert/graph commits can still lose pending data.

Graph cleanup follows the retained alert IDs on startup, age expiry, capacity eviction and explicit clear, including temporary graph files. Failed deletions are retried. A failed filesystem mount does not automatically format storage. Real-board flash endurance, runtime heap/stack and power-cut validation remain separate from the host tests.
