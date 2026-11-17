# Arduino CLI Integration (Windows)

All commands below work in PowerShell or Git Bash. Quote paths that contain spaces.

## 1. Verify installation

```
arduino-cli version
arduino-cli core list
arduino-cli board list
```

- If `arduino-cli` is not on PATH, the Arduino IDE 2.x bundles one at `%LOCALAPPDATA%\Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe`. In PowerShell, call it as `& "<path>" <args>`. Check there before saying the CLI is missing.
- `core list` must show `esp32:esp32` (Espressif's core; `arduino:esp32` is a different, Arduino-branded core for Nano ESP32 — don't confuse the FQBNs). Record its version: Arduino-ESP32 core 2.x and 3.x differ in ADC, LEDC and timer APIs (e.g. `analogSetAttenuation`, `ledcAttach` vs `ledcSetup`, `timerBegin` signature). Write sketches that compile on the installed version.
- If the core is missing, tell the user the install commands (`arduino-cli core update-index` then `arduino-cli core install esp32:esp32`); installing is a system change, so ask first.

## 2. Detect board and port

- `arduino-cli board list` shows ports. Generic ESP32-WROOM-32 dev boards with CP210x/CH340 USB bridges often show **"Unknown"** board name — that is normal; the port is still usable.
- Do not assume COM3. Use the port that actually appears; if several appear, ask which one. If none appears: check the USB cable is data-capable, the CP210x/CH340 driver is installed (Device Manager → Ports), and no other program (Arduino IDE serial monitor, PuTTY) holds the port.
- FQBN: for a generic ESP32-WROOM-32 DevKit, `esp32:esp32:esp32` ("ESP32 Dev Module") is the usual choice. Confirm against the board marking. Use `arduino-cli board details -b esp32:esp32:esp32` to list menu options (UploadSpeed, FlashFreq, PartitionScheme, etc.).
- If the application firmware has a known build configuration (partition scheme, flash size), reuse it when compiling diagnostics that will be uploaded to the same board, so the user's NVS/LittleFS data layout is not disturbed unexpectedly. Ask if unknown.

## 3. Compile (no approval needed)

```
arduino-cli compile --fqbn esp32:esp32:esp32 hw-tests/zmpt_waveform
```

- Pass the **sketch folder** (folder name = `.ino` name). Never compile a test by pointing at the application folder.
- For the application itself, from its folder: `arduino-cli compile --fqbn esp32:esp32:esp32 .`
- Use `--build-path <scratchpad>/build-<name>` to keep build output out of the project.
- Report flash/RAM usage from the output.

### Compile-error workflow

1. Read the first error (later errors usually cascade from it). Quote file:line and the message.
2. Identify the likely cause: missing include, core 2.x vs 3.x API change, missing library, type mismatch, duplicate definition from a stray `.ino` in the same folder.
3. Make the minimal source change. For diagnostic sketches, edit freely. For the user's application firmware, only change it if they asked for a fix, and show the diff.
4. Recompile.
5. Document what changed and why.

Missing libraries: report the name; `arduino-cli lib search <name>` / `arduino-cli lib install "<name>"` only with permission.

## 4. Upload (explicit approval required every time)

Before uploading, state: sketch, port, FQBN, and that it **replaces the firmware currently on the board** (the user's application will need to be re-uploaded afterwards). Wait for a yes.

Use a conservative upload speed:

```
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32:UploadSpeed=115200 hw-tests/zmpt_waveform
```

(Replace `COM3` with the detected port. Confirm `UploadSpeed=115200` is listed in `board details`; 115200 or 230400 are conservative choices.)

Upload troubleshooting:
- `Failed to connect ... Wrong boot mode detected` / `Timed out waiting for packet header`: hold BOOT, press/release EN, release BOOT when "Connecting..." appears. Something on GPIO0, GPIO2 or GPIO12 (strapping pins) may be holding the boot mode wrong — see esp32-diagnostics.md.
- `could not open port ... Access is denied`: another program has the port (close any monitor first).
- Upload succeeds but nothing prints: baud mismatch, or the board reset into download mode — press EN.

## 5. Bounded serial capture

`arduino-cli monitor` runs until stopped, so never start it as an unbounded foreground command. Options:

- Run it in the background, read output after a fixed time, then stop it:
  ```
  arduino-cli monitor -p COM3 -c baudrate=115200
  ```
- Or capture a fixed window with PowerShell:
  ```powershell
  $p = New-Object System.IO.Ports.SerialPort 'COM3',115200
  $p.ReadTimeout = 500; $p.DtrEnable = $false; $p.RtsEnable = $false
  $p.Open(); $end = (Get-Date).AddSeconds(15); $out = ''
  while ((Get-Date) -lt $end) { try { $out += $p.ReadExisting() } catch {} ; Start-Sleep -Milliseconds 100 }
  $p.Close(); $out
  ```
  DTR/RTS on many ESP32 boards drive EN/GPIO0; opening the port with them asserted can reset the board or hold it in download mode. Note whether a reset happened in your interpretation.
- To send a command (e.g. `1` to energize a relay in a test sketch), send it only after the user approved that specific action: `$p.Write('1')`.

Always close the port before uploading.

## 6. Record

For each run, record: core version, FQBN and options, port, sketch name, compile result, upload (yes/no, approved by user), and the raw captured lines used as evidence.
