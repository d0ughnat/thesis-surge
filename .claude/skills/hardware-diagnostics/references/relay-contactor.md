# Relay Module and Contactor Troubleshooting

Read [electrical-safety.md](electrical-safety.md) first. Tests A–D are done with **no mains connected to any contact or load**. Test E involves the real system and requires the user's explicit go-ahead and, for any energized mains part, qualified personnel.

## Identify before testing

For the relay module:
- Channels, relay part number and coil voltage (printed on the relay, e.g. `SRD-05VDC-SL-C` = 5 V coil).
- Trigger type: active-LOW or active-HIGH (jumper "H/L" on some modules). Do not assume — test it (Test B).
- Optocoupler present? JD-VCC / VCC jumper: with the jumper fitted, the coil runs from VCC; removed, the coil needs separate JD-VCC supply and ground.
- Input logic threshold: many 5 V modules need the IN pin pulled near GND to activate; a 3.3 V HIGH may not fully turn off an active-low module powered at 5 V (the LED stays dimly lit / relay stays on). Note this as a hypothesis to check.
- Contacts: COM, NO (normally open), NC (normally closed). Contact rating on relay body (e.g. 10 A 250 VAC).

For the contactor (e.g. YCCH6 family):
- Coil terminals A1/A2 vs power terminals (1/2, 3/4 ... or L/T markings).
- Coil rated voltage and **AC or DC** (and frequency) from the label. Coil rated power/inrush if available.
- Contact configuration: NO, NC or mixed (some modular contactors are offered as normally-closed variants — this inverts the load logic). Read the label/datasheet.
- Pick-up voltage (typically ≥85 % of rated) and drop-out voltage — from the datasheet if available.

Record how firmware expresses "load ON" (e.g. relay energized vs relay de-energized) and check it against the actual NO/NC wiring of both relay and contactor. A double inversion (active-low module + NC contactor) is a common source of "it works backwards".

## Test A — ESP32 GPIO

Goal: the chosen GPIO produces correct levels. Relay module **disconnected** from the pin.

Instrument: DMM (or LED + 1 kΩ to GND).

Sketch: use the safe output sketch below with nothing connected.

Expected: pin reads ~0 V for LOW and ~3.2–3.3 V for HIGH after the matching command; level is stable; no reset. Also observe the pin through reset/boot (DMM or scope) to see whether it glitches.

FAIL indicators: no change (wrong pin, pin is input-only 34–39, pin used by something else in firmware), level drops a lot with a light load (damaged pin).

## Test B — Relay module (coil side only)

Goal: module switches on command and the trigger polarity is known. **Nothing connected to COM/NO/NC.**

Setup: module VCC/GND from a measured 5 V supply (or as the module requires), IN from the tested GPIO, common ground (unless deliberately isolated through JD-VCC).

Steps:
1. Start with the sketch's OFF level; observe the module LED and relay (should be idle).
2. Send the ON command; expect click and LED on. Send OFF; expect release.
3. Measure module VCC during ON (sag ≤ ~0.25 V).
4. Measure IN pin voltage in both states.

Results interpretation:
| Observation | Likely cause |
|---|---|
| No click, LED off in both states | Module unpowered, IN not connected, wrong pin, JD-VCC jumper missing |
| LED changes, no click | Coil supply missing (JD-VCC), supply too weak, failed coil |
| Works inverted | Trigger polarity is opposite to firmware assumption |
| Stays ON / LED dim in OFF state | 3.3 V HIGH insufficient for 5 V active-low module input |
| Click then ESP32 resets | Supply sag/brownout — see esp32-diagnostics.md Stage 2 |
| Chatter | Marginal supply, floating input, firmware toggling |

## Test C — Relay contact verification (de-energized)

Goal: contacts actually close/open. **No voltage on contacts.**

Instrument: DMM continuity/resistance.

Steps: relay OFF → measure COM–NC (expect < 1 Ω) and COM–NO (expect open). Relay ON → COM–NO (< 1 Ω), COM–NC open. Repeat 5–10 cycles; record any intermittent results.

Note: dry-circuit resistance can be slightly higher on contacts designed for power loads; a few hundred mΩ is acceptable, an intermittent open is not.

## Test D — Contactor coil verification

Goal: coil engages reliably at its rated supply.

- **De-energized checks (anyone):** coil resistance A1–A2 (record; open = failed coil; near 0 = shorted). Power contacts by continuity with the coil unpowered: NO open / NC closed.
- **Low-voltage coil (e.g. 12/24 V):** power the coil directly from a current-limited bench supply at rated voltage, without the relay. Expect a firm pull-in, contacts change state (check with DMM continuity on power terminals, which carry no voltage), coil current stable. Then repeat at ~85 % of rated to check pick-up margin.
- **230 V AC coil:** energized testing by qualified personnel only, behind protection, with the power contacts unloaded. Provide them: expected coil voltage, what to observe (pull-in, hum level, contact state), and readings to record.

Then connect relay contact → coil circuit as designed, still with **no load on the contactor's power contacts**, and command the relay. Measure coil voltage at A1–A2 in both states (low-voltage coils), or have a qualified person do it (mains coils).

## Test E — Integrated control-system validation

Only after A–D pass and with the user's explicit approval for this stage.

- Run the application firmware (or a controlled test build). Confirm firmware's notion of ON/OFF matches the physical contact state at each transition.
- Test protection logic with safe stimuli first (e.g. variac-driven sensor input at low voltage behind isolation, or firmware test hooks) before any real load.
- Observe: no rapid ON/OFF cycling, no resets at contactor switching, sensor readings not disturbed by switching events, state survives reboot as designed.
- Real-load testing: qualified personnel, protection in place.

## Fault tree

### Relay not clicking
1. Module power (measure VCC, JD-VCC jumper).
2. IN signal (measure at module pin during ON command).
3. Polarity (try opposite level via sketch).
4. Logic threshold (3.3 V signal vs 5 V module).
5. Firmware: correct pin, `pinMode` called, no later code overriding the pin, no conflicting use (LED/button on same pin).
6. Failed relay/transistor/optocoupler on module.

### Relay clicks but contactor does not engage
1. Wrong contact used (NO vs NC) — Test C.
2. Coil supply absent or wrong type (AC vs DC) / voltage — measure at A1–A2.
3. Coil circuit open: loose terminal, wrong terminal (power terminal instead of coil).
4. Relay contacts failed under coil inrush (pitted) — test continuity after many cycles.
5. Coil voltage sags on pull-in (weak supply, long thin wiring) — measure during actuation.
6. Contactor is NC type: it "engages" (opens) when energized; check expectation.
7. Failed coil (resistance check).

### Intermittent contactor activation
- Loose connections (screw terminals, Dupont leads), marginal coil voltage near pick-up, relay contact bounce/failure, ESP32 brownout/reset toggling the pin, EMI from contactor feeding back into ESP32 or IN line, firmware toggling (protection hysteresis too small, debounce missing).

### Rapid ON/OFF switching (chatter)
- Firmware: thresholds without hysteresis/confirm time, readings disturbed by the switching itself (sensor coupled to load/relay supply), watchdog/reset loops (check reset reason — each boot drives the pin).
- Hardware: AC coil on DC or undervoltage AC (hum/chatter), relay supply sagging when coil pulls in → relay drops → supply recovers → repeats.

### Electrical noise and inductive transients
- Relay module coils normally have flyback diodes on board; confirm.
- Contactor coil suppression: AC coil → RC snubber or MOV rated for coil voltage across A1–A2; DC coil → flyback diode (reverse-biased across coil). Mains-voltage coil suppression installed by qualified personnel.
- Keep mains/coil wiring physically separated from sensor and signal leads; twisted pairs for sensor leads; single-point ground; 100 nF at ADC inputs.
- Correlate: do resets/glitches occur on relay transition, contactor pull-in, or contactor drop-out? Drop-out spikes point to missing coil suppression.

## Safe output test sketch (Tests A and B)

Starts OFF, changes state only on serial command. Never toggles automatically.

```cpp
// hw-tests/relay_gpio/relay_gpio.ino
// Commands: '1' = assert (relay coil ON), '0' = release (OFF), '?' = status.
#include <Arduino.h>

const int PIN = 26;              // VERIFY against wiring/firmware; avoid strapping pins
const bool ACTIVE_LOW = true;    // VERIFY with Test B; true = IN LOW energizes the relay coil

const int ON_LEVEL = ACTIVE_LOW ? LOW : HIGH;
const int OFF_LEVEL = ACTIVE_LOW ? HIGH : LOW;
bool on = false;

void apply() {
  digitalWrite(PIN, on ? ON_LEVEL : OFF_LEVEL);
  Serial.printf("pin=%d coil=%s level=%s\n", PIN, on ? "ON" : "OFF",
                digitalRead(PIN) ? "HIGH" : "LOW");
}

void setup() {
  digitalWrite(PIN, OFF_LEVEL);   // set level before enabling output to avoid a boot pulse
  pinMode(PIN, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.printf("ready pin=%d active_low=%d (send 1/0/?)\n", PIN, ACTIVE_LOW);
  apply();
}

void loop() {
  if (!Serial.available()) return;
  char c = Serial.read();
  if (c == '1') { on = true; apply(); }
  else if (c == '0') { on = false; apply(); }
  else if (c == '?') apply();
}
```

Note: `digitalRead` on an output reports the driven level, not proof that the module received it — confirm with a DMM at the module IN pin.

## Relay/contactor result template

```
A GPIO .............. PASS/FAIL/NOT TESTED — levels measured: LOW=<> HIGH=<>
B Relay module ...... PASS/FAIL/NOT TESTED — trigger=<active-low/high>, VCC idle/ON=<>/<>
C Relay contacts .... PASS/FAIL/NOT TESTED — COM-NO off/on=<>/<>, COM-NC off/on=<>/<>, cycles=<>
D Contactor coil .... PASS/FAIL/NOT TESTED — coil=<V AC/DC>, R=<>, pull-in at <V>
E Integration ....... PASS/FAIL/NOT TESTED — <observations>
```
