# ESP32 Diagnostics (ESP32-WROOM-32)

Covers Stage 1 (health check) and Stage 2 (power validation), plus the GPIO/ADC rules used by every other stage.

## GPIO rules for ESP32-WROOM-32

| Pins | Constraint |
|---|---|
| GPIO6–11 | Connected to internal SPI flash. Never use. |
| GPIO34, 35, 36 (VP), 39 (VN) | Input only. No internal pull-up/pull-down. Fine for ADC. |
| GPIO0 | Strapping: LOW at reset = download mode. Has pull-up. Avoid loads that pull it low at boot. |
| GPIO2 | Strapping: must be LOW/floating to enter download mode. Often on-board LED. |
| GPIO5, GPIO15 | Strapping (SDIO timing / boot log). Output signals at boot. |
| GPIO12 (MTDI) | Strapping: HIGH at reset selects 1.8 V flash → boot failure / garbled boot on most WROOM-32 modules. Never pull high at boot. |
| GPIO1 (TX0), GPIO3 (RX0) | USB serial. Using them breaks upload/logging. |
| GPIO1, 3, 5, 14, 15 | Emit signals/PWM during boot — can glitch a relay. |
| ADC2 (GPIO0, 2, 4, 12–15, 25–27) | ADC2 is unavailable while Wi-Fi is running. `analogRead` returns errors/garbage. |
| ADC1 (GPIO32–39) | Use these for all analog sensing. |

Checklist when reviewing firmware:
- [ ] Every pin in the pin map is unique (no two functions on one GPIO, including LEDs, buttons, relay, sensors).
- [ ] No pin is in GPIO6–11; no output on GPIO34–39.
- [ ] Analog inputs are ADC1 if Wi-Fi is used anywhere.
- [ ] Inputs on 34–39 that need a defined level have external resistors.
- [ ] Relay/contactor outputs are not on strapping pins or boot-glitching pins (prefer GPIO 4, 16–19, 21–23, 25–27, 32–33).
- [ ] Output level is written *before* `pinMode(pin, OUTPUT)` so the relay does not pulse at boot. Note: between reset and `setup()` the pin floats/high-Z; active-low relay modules with input pull-ups may still click at power-up. Verify with the user.
- [ ] Buttons have debounce and a defined idle level.

## ADC facts

- 12-bit (0–4095). Default attenuation in Arduino-ESP32 is 11 dB (called 12 dB in newer IDF): usable range roughly 0.15 V–3.1 V, **non-linear near both ends**. Readings near 0 or above ~3.1 V saturate; 0 counts does not mean 0 V.
- Never apply more than 3.3 V (VDD) to an ADC pin. A 5 V-powered sensor whose output can exceed 3.3 V needs a divider or level shifting.
- `analogReadMilliVolts()` applies eFuse calibration and is preferable to `counts * 3300 / 4095` for absolute DC voltages (offset measurements). Raw counts are fine for relative AC statistics.
- Noise: expect ±several counts to tens of counts. Averaging, a 100 nF capacitor at the pin and short wiring help. Wi-Fi TX bursts add noise.
- `analogRead` takes on the order of 10–20 µs. Interleaving two channels halves each one's effective sample rate. Firmware RMS windows must cover an integer number of mains cycles (or many cycles) to be stable.
- Sensor output impedance: the ESP32 ADC samples into a small capacitor; very high-impedance sources (e.g. 470 kΩ bias dividers without a capacitor) read low/noisy.

## Reset reasons

| `esp_reset_reason()` | Meaning | Typical causes |
|---|---|---|
| `ESP_RST_POWERON` | Power applied | Normal first boot |
| `ESP_RST_EXT` | EN pin | EN button, DTR/RTS from serial adapter |
| `ESP_RST_SW` | `ESP.restart()` | Firmware-requested |
| `ESP_RST_PANIC` | Exception/abort | Null pointer, stack overflow, assert. Boot log shows `Guru Meditation` and a backtrace |
| `ESP_RST_INT_WDT` | Interrupt WDT | ISR too long, interrupts disabled too long |
| `ESP_RST_TASK_WDT` | Task WDT | Loop blocked (long `delay`-free busy loops, blocking network calls) |
| `ESP_RST_WDT` | Other WDT | |
| `ESP_RST_BROWNOUT` | Supply dipped below ~2.4–2.8 V | Weak USB, long thin wires, relay coil inrush, Wi-Fi TX peaks, contactor transients |
| `ESP_RST_DEEPSLEEP` | Wake from deep sleep | |

Brownout boot log also prints `Brownout detector was triggered`. Don't disable the brownout detector to "fix" resets — that hides a real supply problem and risks flash corruption.

Decoding a panic backtrace: copy the `Backtrace:` line; with the build's `.elf`, use the ESP Exception Decoder or `xtensa-esp32-elf-addr2line -pfiaC -e <sketch>.ino.elf <addresses>`.

## Stage 1: health-check sketch

Purpose: proves the board boots, the serial link works, reports reset reason, and shows heap stays stable. Drives no outputs.

```cpp
// hw-tests/esp32_health/esp32_health.ino
#include <esp_system.h>
#include <esp_chip_info.h>

const uint32_t BAUD = 115200;

const char* resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default: return "OTHER";
  }
}

void setup() {
  Serial.begin(BAUD);
  delay(500);
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  Serial.printf("boot reset=%s chip_model=%d rev=%d cores=%d\n",
                resetName(esp_reset_reason()), chip.model, chip.revision, chip.cores);
  Serial.printf("cpu_mhz=%u flash_bytes=%u sdk=%s\n",
                (unsigned)getCpuFrequencyMhz(), (unsigned)ESP.getFlashChipSize(), ESP.getSdkVersion());
}

void loop() {
  static uint32_t n = 0;
  Serial.printf("alive n=%lu uptime_ms=%lu heap=%u min_heap=%u\n",
                (unsigned long)n++, (unsigned long)millis(),
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
  delay(1000);
}
```

Expected (PASS): one `boot` line with `reset=POWERON` or `EXT`, then `alive` lines every ~1 s with `n` incrementing without restarts and `min_heap` stable. FAIL indicators: repeated `boot` lines, `BROWNOUT`/`PANIC`/`*_WDT`, garbage characters (baud mismatch or supply noise), no output at all.

## Stage 2: power supply validation

Instruments: DMM; oscilloscope if available (for dips). Measure with the system at idle, then with the relay module energized (low-voltage side only), then with Wi-Fi active if the application uses it.

| Rail | Measure at | Expected | Notes |
|---|---|---|---|
| 5 V / VIN | ESP32 5V pin to GND | 4.75–5.25 V | USB ports sag under relay coil load |
| 3.3 V | ESP32 3V3 pin to GND | 3.20–3.40 V | Dips below ~3.0 V cause brownout |
| Relay module VCC / JD-VCC | module pins | per module rating (often 5 V) | Coil draws ~70–90 mA per channel at 5 V |
| Sensor supplies | each module's VCC | per datasheet | ACS712 is rated 4.5–5.5 V |
| Contactor coil supply | coil terminals A1–A2 (low-voltage coils only) | label rating ±10 % | Mains coils: qualified personnel only |

Checklist:
- [ ] Common ground between ESP32, sensor modules, relay module logic side (unless the relay module is deliberately opto-isolated with separate JD-VCC ground).
- [ ] Power source capability: USB port vs dedicated 5 V supply with ≥1 A headroom.
- [ ] Voltage under load (relay ON) differs from idle by less than ~0.25 V on 5 V and ~0.1 V on 3.3 V.
- [ ] Bulk capacitance near the ESP32 (e.g. 100–470 µF + 100 nF) if dips are observed.
- [ ] Wire gauge and length on supply leads; loose breadboard contacts.
- [ ] If resets correlate with contactor switching (not with relay coil alone), suspect inductive transients/EMI — see relay-contactor.md.

Ask the user to report each measured value; mark rails not measured as NOT TESTED.

## Common ESP32 symptom map

| Symptom | First checks |
|---|---|
| Resets when relay energizes | Reset reason (BROWNOUT?), 5 V under load, relay supply path, bulk cap |
| Resets when contactor switches | Coil suppression, wiring separation, ground path; reset reason may be BROWNOUT or PANIC from corrupted signals |
| Relay clicks at boot | Pin choice (strapping/boot glitch), active-low module with pin floating before `setup()`, write level before `pinMode` |
| Analog reads 0 or 4095 constantly | Wrong pin, ADC2 + Wi-Fi, sensor unpowered, input above 3.3 V / below 0 V, broken wire |
| Analog reading noisy | Wi-Fi bursts, missing filter cap, long unshielded leads, shared ground with relay currents |
| Garbage on serial | Baud mismatch, boot ROM messages at 115200 vs app baud, supply noise |
| Upload fails | Strapping pin held by external circuit (GPIO0/2/12), port in use, cable |
