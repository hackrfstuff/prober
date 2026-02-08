# prober

Native CLI + GUI tool for flashing and configuring Bluejay ESC firmware over BLHeli 4-way passthrough (MSP). Supports single ESCs, 4-in-1 boards, and AIOs. Also includes a C2 flashing mode for direct SiLabs programming via Arduino.

Built in C++ with no runtime dependencies. Ships as a single portable binary (CLI) or Qt-based GUI.

---

## Features

### Flashing
- Flash one or all ESCs in a single run (`--index N` or `--all`)
- Bundled Bluejay firmware auto-resolution from ESC identity (`--hex auto`)
- Intel HEX file support with full address range handling
- Post-flash verification: byte-level (`--verify full`), page CRC (`--verify fast`), or skip
- EEPROM erase option (`--erase-eeprom`)
- Full application erase before write (`--full-erase-app`, `--full-erase-entire-app`)
- Dry-run mode for testing without writing (`--dry-run`)
- Settings preservation across flashes (default), with optional erase or migrate
- Inline settings overrides applied immediately after flash (`--set KEY=VALUE`)

### Settings Read & Write
- Read firmware identity, version, target layout, MCU, and all Bluejay settings from any ESC
- Structured display output matching esc-configurator field names
- Write individual settings without reflashing (`--write-settings --set KEY=VALUE`)
- Supports all Bluejay EEPROM fields: timing, demag, direction, PWM frequency, braking, EDT, beacon, temperature protection, power rating, LED control, startup melody region, and more
- Multi-round read with configurable retries and backoff for flaky links (`--read-rounds`, `--read-round-sleep-ms`)

### 4-in-1 / AIO Stability
- **Automatic mapping resolution** — detects whether the FC uses 0-based or 1-based ESC indexing without user input. Two-phase scoring (quick discriminator + full probe) locks the mapping before any flash or read operation.
- **Value-based warm-up** — wakes all ESC bootloaders before mapping is decided by sending raw selection values across both index ranges. No mapping assumption is made during warm-up.
- **Direct-kick recovery** — if an ESC fails index-select but responds to direct addressing, the tool automatically kicks it via direct, resets, and retries index-select. Includes session rebuild (exit + re-enter 4-way) as a fallback. This runs during the reachability probe, during pre-flash warmup, and again at flash time — an ESC that is alive will not be skipped.
- **Mode-aware reset** — `ExitBootloader` resets send the mode byte (2-byte format first) so the ESC actually releases the bus. Reset format is auto-detected and cached per session to avoid unnecessary bus traffic.
- **Post-flash channel warm-up** — after each ESC is flashed, all channels are briefly re-woken to prevent the next ESC from being cold or stuck.
- **Graceful degradation** — if an ESC is genuinely unreachable after all recovery attempts, it is skipped and the rest of the batch continues. Use `--skip-missing` to suppress the abort.

### Probing & Diagnostics
- `--probe-escs` — test reachability of all ESCs under the resolved mapping
- `--probe-sweep` — sweep all selection values 0–255 to discover what the FC responds to
- Direct-address probing for unreachable ESCs (addr 0x0004–0x0008)
- Detailed selection reports: signature, mapping used, attempt count, direct vs index
- `--trace` logging shows every 4-way transaction, reset strategy, capability cache state, and recovery decisions

### C2 Flashing (SiLabs direct)
- Flash Bluejay onto bare SiLabs MCUs via an Arduino-based C2 interface
- `--c2-install` — upload the C2 interface firmware to an Arduino Uno or Nano (via avrdude)
- `--c2-detect` — check if the C2 interface is present
- `--c2-read-info` — read SiLabs device ID and revision
- `--c2-erase` — erase the target MCU
- `--c2-write-hex` — write a hex file to the target
- `--c2-reset` — reset the target

### GUI
- Qt-based interface with real-time progress, per-ESC status, and settings editor
- NDJSON IPC protocol between GUI and CLI backend (`--ui-json`)
- Serial port auto-detection and selection

---

## CLI Reference

### Basic Usage

```sh
# Flash all ESCs
prober --port COM5 --hex firmware.hex --all --verify

# Flash one ESC (1-based index)
prober --port COM5 --hex firmware.hex --index 2 --verify fast

# Auto-resolve Bluejay firmware from ESC identity
prober --port COM5 --hex auto --all --verify

# Read settings from all ESCs
prober --port COM5 --read --all

# Write a setting to all ESCs
prober --port COM5 --write-settings --all --set BRAKE_ON_STOP=On

# Flash + apply settings in one pass
prober --port COM5 --hex firmware.hex --all --verify --set MOTOR_DIRECTION=2
```

### Connection

| Flag | Default | Description |
|------|---------|-------------|
| `--port PORT` | auto-detect | Serial port |
| `--baud N` | 115200 | Baud rate |
| `--timeout S` | 1.0 | 4-way response timeout (seconds) |
| `--delay S` | 0.0 | Inter-byte delay |
| `--safe` | off | BB51 safe defaults (timeout=6s, delay=0.01s) |
| `--list-ports` | | List available serial ports and exit |

### Target Selection

| Flag | Description |
|------|-------------|
| `--index N` | Flash/read ESC N (1-based) |
| `--all` | Flash/read all ESCs |
| `--mapping MODE` | `auto`, `strict-0-based`, `strict-1-based`, `prefer-0-based`, `prefer-1-based` |
| `--skip-missing` | Continue batch even if some ESCs are unreachable |

### Flash Options

| Flag | Default | Description |
|------|---------|-------------|
| `--hex PATH` | required | Hex file path, or `auto` for bundled Bluejay |
| `--verify [full\|fast\|off]` | off | Post-flash verification mode |
| `--erase-eeprom` | off | Erase EEPROM (settings) during flash |
| `--full-erase-app` | off | Erase full application area before write |
| `--full-erase-entire-app` | off | Erase entire application region |
| `--dry-run` | off | Simulate flash without writing |
| `--verify-all-bytes` | off | Verify every byte (not just written pages) |
| `--assume-sig SIG` | | Override device signature detection |
| `--settings MODE` | preserve | `preserve`, `erase`, or `migrate` |
| `--set KEY=VALUE` | | Apply setting after flash (repeatable) |
| `--bluejay-version VER` | 0.21.0 | Bluejay version for `--hex auto` |
| `--bluejay-dir PATH` | | Override firmware search directory |

### Read & Write Settings

| Flag | Default | Description |
|------|---------|-------------|
| `--read` | | Read and display ESC settings |
| `--read-rounds N` | 2 | Retry rounds for flaky reads |
| `--read-round-sleep-ms N` | 150 | Sleep between read rounds |
| `--write-settings` | | Write settings without reflashing |
| `--set KEY=VALUE` | | Setting to write (repeatable) |

### Probe & Debug

| Flag | Description |
|------|-------------|
| `--probe-escs` | Probe reachability of all ESCs |
| `--probe-sweep` | Sweep all selection values 0–255 |
| `--probe-tries N` | Attempts per probe (default: 6) |
| `--probe-sleep S` | Sleep between probe attempts (default: 0.15) |
| `--settle-ms N` | Post-passthrough settle time (default: 200) |
| `--trace` | Enable detailed 4-way transaction logging |
| `--loglevel LEVEL` | `INFO`, `DEBUG`, or `TRACE` |

### Flash Timing (advanced)

| Flag | Default | Description |
|------|---------|-------------|
| `--flash-inter-esc-ms N` | 250 | Delay between ESCs in batch flash |
| `--flash-preselect-tries N` | 2 | Warmup selection attempts before flash |
| `--flash-preselect-sleep-ms N` | 150 | Sleep between preselect retries |
| `--flash-post-select-ms N` | 200 | Settle after selection |
| `--flash-erase-retries N` | 3 | Page erase retry count |
| `--flash-erase-inter-page-ms N` | 50 | Delay between page erases |
| `--flash-write-retries N` | 3 | Write block retry count |
| `--flash-write-inter-block-ms N` | 10 | Delay between write blocks |
| `--verify-read-retries N` | 3 | Verification read retry count |

### C2 Mode

| Flag | Description |
|------|-------------|
| `--c2` | Enable C2 mode |
| `--c2-port PORT` | Serial port for C2 interface (required) |
| `--c2-install uno\|nano` | Upload C2 interface firmware to Arduino |
| `--c2-install-hex PATH` | Explicit path to interface firmware hex |
| `--c2-detect` | Check if C2 interface is present |
| `--c2-read-info` | Read target device ID and revision |
| `--c2-erase` | Erase target MCU |
| `--c2-write-hex PATH` | Write hex file to target |
| `--c2-reset` | Reset target |
| `--c2-timeout-ms N` | C2 command timeout (default: 2000) |
| `--c2-connect-delay-ms N` | Wait after opening port (default: 2000) |

### Output

| Flag | Description |
|------|-------------|
| `--json` | JSON output for `--list-ports` |
| `--ui-json` | NDJSON event stream for GUI integration |

---

## Settable Keys

Use with `--set KEY=VALUE` (for `--write-settings` or inline with flash):

| Key | Range | Notes |
|-----|-------|-------|
| `STARTUP_POWER_MIN` | 1000–1125 | Display value (µs) |
| `STARTUP_POWER_MAX` | 1004–1300 | Display value (µs) |
| `COMMUTATION_TIMING` | 1–5 | 1=Low … 5=High |
| `DEMAG_COMPENSATION` | 1–3 | 1=Off, 2=Low, 3=High |
| `RPM_POWER_SLOPE` | 0–13 | |
| `BEEP_STRENGTH` | 0–255 | |
| `BEACON_STRENGTH` | 0–255 | |
| `BEACON_DELAY` | 1–5 | |
| `POWER_RATING` | 1–2 | 1=1S, 2=2S+ |
| `TEMPERATURE_PROTECTION` | 0–7 | |
| `FORCE_EDT_ARM` | On / Off | |
| `BRAKE_ON_STOP` | On / Off | |
| `BRAKING_STRENGTH` | 0–255 | |
| `MOTOR_DIRECTION` | 1–4 | |
| `LED_CONTROL` | 0–4 | |
| `STARTUP_BEEP` | 0–3 | |
| `DITHERING` | On / Off | |
| `PWM_FREQUENCY` | 0 / 24 / 48 / 96 | 0=Dynamic |

---

## Build

Requires Visual Studio 2022+, CMake, Ninja. Qt 6.x for the GUI (optional).

### CLI only

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

### CLI + GUI

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build -j
```

### Portable distribution (Windows)

The package script builds everything, deploys Qt runtime, bundles vc_redist, and runs sanity checks:

```sh
.\scripts\package_windows.ps1 -QtPath "C:\Qt\6.x.x\msvc2022_64"
```

Or via the .cmd wrapper:

```sh
.\scripts\package_windows.cmd -QtPath "C:\Qt\6.x.x\msvc2022_64"
```

Output goes to `dist/` with this layout:

```
dist/
  prober.exe
  prober_gui.exe
  vc_redist.x64.exe
  tools/
    gui/
    avrdude/
    c2_firmware/
    bluejay_firmware/
```

---

## License

This project is licensed under the [GNU Affero General Public License v3.0](LICENSE).