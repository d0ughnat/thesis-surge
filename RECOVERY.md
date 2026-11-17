# Automatic recovery

The complete implementation is in `recovery.h`, `relay.ino`, and `config.h`.
Installed headers identify Arduino-ESP32 3.3.12. The watchdog uses the IDF 5+
configuration structure, reconfigures an already initialized watchdog, enables
panic on timeout, and subscribes loopTask and acquisition explicitly. An IDF 4
compatibility branch uses the older seconds/boolean API. The older branch has
not been compiled here. Target is classic ESP32, including ESP32-D0WD; this does
not verify your board's flash, regulator, wiring, or partition selection.

## Behavior

- Watchdog deadline: 30 seconds. Acquisition feeds only after a completed ADC,
  protection, GPIO and publication cycle. Main feeds after HTTP, serial,
  persistence, graph processing and WiFi servicing, provided sampling is fresh.
  Invalid measurements and storage/network failures are handled outcomes, not
  reasons to stop feeding. No timer/interrupt blindly feeds on behalf of a task.
- A main or acquisition processing gap above 2.5 seconds inhibits the load.
  Acquisition checks the main heartbeat before its next measurement. If
  acquisition stalls, main inhibits when it next finishes its own processing.
  A permanent mutex deadlock can prevent these checks; the watchdog remains the
  fallback. These are scheduling thresholds, not guaranteed electrical trip times.
- Every startup commands OFF before configuring the output. All ON requests
  pass a common recovery inhibit, including sensor-power and manual overrides.
  Protection starts locked out while measurements and dashboard continue.
  After 10 seconds of healthy processing, release and press the local GPIO25
  reset button. Existing 30-second safe-voltage qualification and durable trip
  reservation must then succeed before ON. The button also retains its original
  trip-lockout reset behavior. No remote acknowledgement was added.
- Three boots without a 120-second healthy run select recovery mode: the load
  remains inhibited and local acknowledgement requires 60 seconds of healthy
  processing. This does not disable watchdog coverage; persistent faults may
  still reset, but cannot automatically reconnect the load. A held button does
  not bypass the release/press debounce.
- RTC-retained counters have a magic/check field, saturate, and write no flash.
  Boot streak clears after 120 seconds of healthy processing; recovery mode for
  the current boot remains latched. Counters are diagnostic, not a lifetime
  power-cycle record: power loss, brownout, or RTC corruption may clear them.
  Every boot still locks out even if retention is lost.
- Watchdog initialization/subscription failure inhibits the load. Acquisition
  task creation failure stays in a diagnostic OFF mode rather than deliberately
  rebooting forever. Allocation failures earlier in startup retain OFF and may
  be recovered by the watchdog.
- WiFi starts without a setup wait. Driver disconnection events allow retries
  every `WIFI_RETRY_MS`; an unfinished attempt is cancelled first and retried
  after a disconnection event. No retry delay loop, scan wait, or router-driven
  restart is used. The existing server remains running across reconnections;
  a new DHCP address is printed if needed. Network loss alone cannot establish
  that rebooting will help, so it never triggers a reboot.
- No change to RMS thresholds, sensor acquisition, surge confirmation,
  brownout registers, or normal voltage protection during relay switching.
  Garbled serial bytes are not used as a fault detector.

## Diagnostics

Serial remains 115200 baud, 8-N-1. Startup prints a symbolic and numeric
`esp_reset_reason()`, retained boot count, unstable-boot count, watchdog result,
and IDF version. Examples:

```text
BOOT reset=TASK_WATCHDOG (6), retained_boots=2, unstable_boots=2
RECOVERY load OFF; local reset acknowledgement required.
RECOVERY watchdog=armed, timeout_ms=30000, IDF=...
```

`/data` retains the numeric `reset_reason` and adds `reset_reason_name`,
`recovery_hold`, `recovery_crash_loop`, `watchdog_ok`, `retained_boot_count`,
and `unstable_boot_count`. Existing heap, stack, sample age, heartbeat, storage,
IP and RSSI diagnostics remain available. Named reset reasons include brownout,
task/interrupt/other watchdog, panic, software, power-on, external, deep sleep,
and SDIO. Some reset causes can be reported generically; reset cause alone
does not prove which external electrical disturbance occurred.

## Hardware and corrupted serial

An IP/RSSI log establishes that WiFi reached that point, not that the supply
remained healthy afterward. Check whether a new BOOT line, boot ID, or uptime
reset accompanies the corruption. Intentionally changing the monitor baud
corrupts the display without crashing firmware. Opening Serial Monitor can also
toggle DTR/RTS and reset some development boards. Classic ESP32 boot logging is
normally 115200; distinguish ROM/panic output from application output.

Compare laptop USB power against the external supply with identical firmware.
Scope 3.3 V at the module, regulator input, EN and ground during WiFi transmit
and relay switching. A multimeter can miss short dips. Investigate regulator
current/transient capability, input headroom, heating, long/thin wires, loose
grounds, USB cable/bridge noise, and coupling into UART/EN/ADC wiring.

Espressif recommends a 3.3 V chip supply capable of at least 500 mA and local
decoupling; allow additional capacity for your board and peripherals. Supply a
development board's USB/5V input according to its schematic; do not feed 5 V
to 3V3. Use appropriate nearby ceramic and bulk decoupling, short supply loops,
separate coil-current return paths, and an adequately rated isolated coil
supply where the driver design permits. Keep UART/ADC wiring away from coil
and mains conductors. Do not accidentally defeat intended galvanic isolation.

Use manufacturer-approved suppression matched to coil type: DC flyback diode
or TVS as appropriate, AC-rated RC snubber/MOV for AC coils. A diode is not
suitable across an AC coil; suppression can change release time, so remeasure
contactor timing. Mains-side suppression, creepage, fusing and contactor changes
require an appropriately qualified electrical installer. Brownout detection
remains enabled; firmware cannot fix a collapsing supply.

**Existing wiring is not fail-open:** GPIO26 LOW energizes the normally closed
YCCH6-02 contactor coil to open the load contacts. Loss of coil power closes
those contacts. Firmware cannot keep a dangerous load OFF through power loss,
boot-time high-impedance pins, or a failed driver. A fail-open contactor/control
arrangement and hardware interlock are required to meet that safety objective.
Verify biasing/driver polarity to hold the intended OFF state before firmware
runs; repeated-reset prevention in software does not provide this guarantee.

Sources: [Espressif watchdog API](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/system/wdts.html),
[power design checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32/schematic-checklist.html),
[reset/brownout FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/system.html).

## Bench verification

Perform firmware fault tests with the dangerous load disconnected, using an
isolated low-voltage simulator/dummy load. Do not generate mains surges merely
to test a watchdog. Verify physical isolation separately with qualified help.

1. Build for the actual classic ESP32 board/flash layout. Serial at 115200.
   Boot: verify OFF, named reset reason, `watchdog=armed`, dashboard available,
   continuing sample timestamps and no automatic ON. After 10 healthy seconds,
   release/press reset and verify 30 seconds of valid 220-235 V qualification
   plus persistence before ON. Invalid voltage must retain OFF.
2. Disable the router longer than 30 seconds. Verify ongoing sample/event
   processing, no reboot, retry messages and automatic dashboard restoration
   when the router returns. Confirm the printed address if DHCP changed it.
3. For a bench-only test build set `RECOVERY_TEST_HOOKS=1` in config.h or via
   the compiler define. Send `{"cmd":"test_main_stall"}` plus newline.
   Acquisition should inhibit OFF after its main-heartbeat freshness deadline;
   watchdog should reset at approximately 30 seconds after the last feed.
   Check TASK_WATCHDOG/panic diagnostics, changed boot ID and mandatory local
   acknowledgement after reboot. A Serial GUI reconnect may miss early logs.
4. Send `{"cmd":"test_acquisition_stall"}`. Main should inhibit OFF once
   sample age exceeds 2.5 seconds; watchdog should recover the suspended task
   through reboot. This hook acquires the mutex before suspending, preventing
   the test itself from deliberately suspending a mutex owner.
5. Repeat a stall promptly after each boot, three times without a healthy
   120-second run. Verify recovery-mode logging, OFF throughout, refusal of
   local reset before 60 healthy seconds, and no automatic load reconnection.
   After 120 healthy seconds verify streak clears. Power loss may clear RTC
   counts but must still require local acknowledgement.
6. Exercise the existing surge demo and a controlled isolated sensor simulator.
   Verify events/trips and uninterrupted acquisition, without reboots merely
   because a surge occurs. Confirm normal three-trip lockout still works.
7. Test baud mismatch separately; dashboard sample progress and boot ID should
   remain unchanged. Switch a safely isolated coil while scoping the supply.
   If a brownout happens, check named reset logging and OFF/acknowledgement on
   restart. Do not disable brownout or repeatedly undervolt a live installation.
8. Return `RECOVERY_TEST_HOOKS` to 0 and rebuild before deployment. Test hook
   commands are compiled out by default; no HTTP fault-injection endpoint exists.

Verification here: the full modified sketch was compiled to an ESP32 object
using installed core 3.3.12 headers/compiler. Arduino CLI's normal build is
blocked by restricted access to its package directory. Final linking, flashing,
physical relay behavior, watchdog timing and electrical tests remain required.
