# Electrical Safety

Read this before any step involving mains voltage, a variac, a contactor's power circuit or a current transformer. When a rule here conflicts with a user request, explain the hazard and offer a safe alternative; do not provide the unsafe procedure.

## Hard limits (never instruct)

- Rewiring, connecting or disconnecting anything while the mains side is energized.
- Bypassing, bridging or removing fuses, breakers, RCDs/GFCIs, interlocks or enclosure covers to "test".
- Probing exposed mains terminals with unrated probes, a microcontroller, a USB-connected scope with grounded reference, or a DMM below CAT III for the circuit.
- Connecting mains (directly or through resistors only) to any ESP32 pin.
- Floating-ground tricks (lifting the scope's earth pin).
- Leaving a current transformer secondary open while primary current flows.
- Energizing a contactor or load automatically from firmware during a test.

## Who does what

- **Low-voltage, isolated work** (ESP32, 3.3/5 V modules, relay coil side, a contactor with a low-voltage coil powered from a current-limited bench supply, sensor outputs): can be guided step by step.
- **Energized mains-side work** (measuring 230 V terminals, contactor power contacts under voltage, variac output, verifying sensor primaries): must be done by qualified electrical personnel with appropriately rated instruments. Provide the expected readings and what to record; do not instruct an unqualified person to do it.
- If the user's qualification or instruments are unknown, ask before giving a mains-side procedure.

## Isolation facts

- **ZMPT101B modules are not safety-rated isolation barriers.** The transformer is isolating, but the PCB creepage/clearance, primary resistor and terminal spacing on cheap modules are not certified. Treat the module's low-voltage side as potentially hazardous if the primary is connected; keep it enclosed; do not touch the ESP32 or USB cable assembly carelessly while the primary is live. Prefer powering the ESP32 from an isolated supply and using a USB isolator for the laptop.
- **ACS712** has internal isolation between the current path and the signal side, but the board-level clearances on breakout modules are often far below the chip rating. Do not route mains-current through an ACS712 module unless its creepage, enclosure and terminal rating suit the voltage; prefer the CT for mains current.
- **SCT-013** is a split-core clamp around an insulated conductor; it never requires breaking into the circuit. Clamp only around a single insulated conductor (live *or* neutral, not both).
- A laptop on USB ties the ESP32 ground to the laptop's ground (and possibly earth via its charger). Any accidental mains-to-ESP32 fault therefore reaches the laptop and the user.

## Preferred low-risk test methods

1. Test ESP32, relay module and logic with **no mains connected at all**.
2. Test a contactor coil with its **rated low-voltage coil supply** (if it is a 24 V/12 V coil) from a current-limited bench supply. For a 230 V AC coil, a qualified person performs the test, or the coil is tested de-energized only (resistance check).
3. Check relay and contactor contacts **de-energized** with a DMM continuity/resistance mode, actuating the coil from a separate safe supply.
4. Test voltage sensors with a **variac behind an isolation transformer**, starting from 0 V, inside a closed enclosure, with an RCD and a fuse on the variac output. Raise voltage slowly while monitoring.
5. Test current sensors with a **low-voltage, high-current source** (e.g. a DC bench supply into a power resistor for the ACS712; multiple turns of wire through the CT with a low-voltage AC source) rather than mains loads, wherever practical.

## Variac precautions

- A variac is an autotransformer: **its output is not isolated from mains**. Neutral and live may be swapped depending on wiring; every output terminal can be at mains potential even at a "0 V" setting.
- Put it behind an isolation transformer for bench work, fuse its output, start at 0, and never touch the output side while energized.
- An independent, rated meter on the variac output is the reference for calibration — not the variac dial.

## Current transformer open-secondary hazard

- A CT is a current source. With the secondary open and primary current flowing, the secondary voltage rises until something breaks down, potentially to hazardous levels, and the core can saturate and overheat.
- The **SCT-013-000 is a current-output type (100 A : 50 mA) without an internal burden resistor**. Some units contain a protective clamp, but do not rely on it. Connect the burden resistor before clamping onto a live conductor; remove the clamp before disconnecting the burden.
- Voltage-output variants (e.g. SCT-013-030, -050) have a built-in burden and must *not* get an extra low-value burden; verify the part number on the label.

## Contactor and inductive loads

- Know the coil rating exactly (voltage, AC or DC, frequency) from the contactor label before energizing. YCCH6-family modular contactors come in several coil voltages and AC/DC variants; do not assume.
- Contactor power terminals may remain live from the supply side regardless of coil state. Lock out the upstream breaker before touching them.
- De-energizing an inductive coil creates a voltage spike. AC coils need an RC snubber or varistor rated for the coil voltage; DC coils need a flyback diode (or diode + Zener for fast release). Only qualified personnel should add suppression on mains-voltage coils.

## Before every energized step, confirm

- [ ] Who is performing it and that they are qualified for that voltage.
- [ ] Instruments and probes are rated (CAT III 300 V minimum for 230 V installations) and inspected.
- [ ] Protective devices (fuse, breaker, RCD) are present and not bypassed.
- [ ] The setup is enclosed, wiring is secured, and no low-voltage side can be touched while energized.
- [ ] The ESP32 ground/USB path is considered (isolated supply or USB isolator preferred).
- [ ] There is a clear plan to de-energize and what readings to record.
