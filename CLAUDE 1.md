# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project summary

ESP32 Bit Pirate is open-source firmware (PlatformIO + Arduino) that turns an ESP32-S3 board into a Bus Pirate–style multi-protocol tool. It exposes a CLI over USB serial, Wi‑Fi (AP/STA, with HTTP + WebSocket) and a Cardputer standalone mode, and supports protocols such as I²C, SPI, UART, 1‑Wire, 2‑Wire, 3‑Wire, DIO, IR, USB, BLE, Wi‑Fi, Ethernet, JTAG, LED, I²S, CAN, Sub‑GHz, RFID, RF24, FM, Cellular, and LoRa. A wiki at https://github.com/geo-tp/ESP32-Bit-Pirate/wiki is the canonical user docs.

## Common commands

All commands are run from the repository root.

```sh
# Install PlatformIO (CI uses Python 3.12 and `python -m pip install --upgrade platformio`)
pipx install platformio        # or: python -m pip install --user platformio
pio --version

# Build a specific board environment (one of the [env:*] sections in platformio.ini)
pio run -e cardputer
pio run -e s3-devkit
pio run -e m5stack-sticks3
pio run -e custom

# Upload to a connected device
pio run -e s3-devkit -t upload

# Serial monitor (default 115200 baud, see platformio.ini `monitor_speed`)
pio device monitor -e s3-devkit

# Run the native (host) test suite — Unity, runs on the build machine, no ESP32 needed
pio test -e native-tests

# Run a single Unity test by name (filter the runner output)
pio test -e native-tests -v | grep -i I2cController

# Clean
pio run -e s3-devkit -t clean
```

The CI workflow (`.github/workflows/ci.yml`) only runs `pio test -e native-tests` on Ubuntu; firmware delivery happens in `.github/workflows/cd.yml` which builds every non-test env, uncomments `-D ENABLE_FASTLED_PROTOCOL_SWITCHES` in `platformio.ini`, and merges `bootloader.bin` + `partitions.bin` + `firmware.bin` into a single `esptool merge-bin` for each board.

## Supported environments (platformio.ini)

Environments are selected with `pio run -e <name>`. Each defines its own pin map and optional libraries. The `native-tests` env is the only one without a `-DDEVICE_*` macro and is for host tests.

| Env                       | DEVICE_* macro         | Notes                                |
|---------------------------|------------------------|--------------------------------------|
| `custom`                  | `DEVICE_CUSTOM`        | User-configurable profile (see below)|
| `s3-devkit`               | `DEVICE_S3DEVKIT`      | Generic ESP32-S3 DevKit              |
| `s3-devkit-n16-r8`        | `DEVICE_S3DEVKIT`      | 16 MB flash / 8 MB PSRAM variant     |
| `cardputer`               | `DEVICE_CARDPUTER`     | M5 Cardputer                         |
| `cardputer-adv`           | `DEVICE_CARDPUTER`+`DEVICE_CARDPUTERADV` | M5 Cardputer ADV        |
| `m5stack-sticks3`         | `DEVICE_STICKS3`       | M5 StickS3                           |
| `m5stack-stamps3`         | `DEVICE_M5STAMPS3`     | M5 StampS3                           |
| `atom-lite-s3`            | `DEVICE_M5STAMPS3`     | M5 AtomS3 Lite                       |
| `t-display-s3`            | `DEVICE_TDISPLAYS3`    | LILYGO T-Display S3                  |
| `t-embed-s3`              | `DEVICE_TEMBEDS3`      | LILYGO T-Embed S3                    |
| `t-embed-s3-cc1101`       | `DEVICE_TEMBEDS3CC1101`| T-Embed + CC1101                     |
| `t-embed-s3-cc1101plus`   | `DEVICE_TEMBEDS3CC1101`| T-Embed CC1101 Plus                  |
| `xiao-esp32s3`            | `DEVICE_S3DEVKIT`      | Seeed XIAO ESP32-S3                  |
| `waveshare-s3-geek`       | `DEVICE_WAVESHARE_S3_GEEK` | Waveshare ESP32-S3-GEEK          |
| `vision-master-t190`      | `DEVICE_VISION_MASTER_T190` | Heltec Vision Master T190       |
| `heltec_wifi_lora_32_V4`  | (heltec-specific)      | Heltec WiFi LoRa 32 V4               |
| `heltec_wifi_lora_32_V3`  | (heltec-specific)      | Heltec WiFi LoRa 32 V3               |
| `native-tests`            | —                      | Unity host tests only                |

## Architecture

`src/main.cpp` is the entry point. The top of that file contains an inline architecture diagram worth re-reading before changes. The flow for a command is:

```
User → TerminalInput → ActionDispatcher → Controller → Service → Controller → TerminalView
```

### Board layer (`src/Boards/`)

Each supported board has its own `*Board` class (`CardputerBoard`, `StickS3Board`, `StampS3Board`, `S3DevKitBoard`, `TDisplayS3Board`, `WaveshareS3GeekBoard`, `TEmbedBoard`, `VisionMasterT190Board`, `CustomBoard`). A board exposes the three things `main.cpp` wires into the rest of the firmware:

- `IDeviceView&` — on-screen UI (M5DeviceView / St7789Spi / St7789Parallel / Cardputer / NoScreen).
- `IInput&` — physical buttons/encoders (DefaultInput, CardputerInput, TEmbedInput, CustomInput, …).
- `IHostSerial&` — USB CDC or UART bridge to the host PC.

`main.cpp` selects the board via `#if defined(DEVICE_*)`. The shared types live under `src/Boards/Common/`. `src/Boards/Custom/CustomBoardConfig.h` is the indirection that maps PlatformIO `-DCUSTOM_*` flags to the generic CustomBoard; users customize a new ESP32-S3 board by editing the `[env:custom]` block in `platformio.ini` only.

### Dispatch / DI (`src/Dispatchers/`, `src/Providers/`)

- `ActionDispatcher` runs the main loop. It owns the `ModeEnum` and routes user input to the right controller. It also handles repeats (`123:read`), pipelines (`a; b; c`) and Bus Pirate–style bytecode instructions.
- `DependencyProvider` is a hand-rolled DI container constructed once per terminal mode. It owns every `Service`, `Controller`, `Transformer`, `Manager`, `Analyzer`, and `Shell` and exposes them as references. The constructors of every `*Controller` accept a `DependencyProvider&` and pull what they need from it.

### Modes and controllers (`src/Controllers/`)

One controller per protocol mode (I2c, Spi, Uart, OneWire, TwoWire, ThreeWire, Infrared, Led, Bluetooth, Wifi, I2s, Jtag, Can, Ethernet, SubGhz, Rfid, Rf24, Cell, Fm, LoRa, Dio, HdUart, UsbS3, Expander, Utility). A controller is the user-facing layer: it parses commands, prompts for input, calls services, and formats output. `Abstracts/ANetworkController` is the shared base for network-protocol controllers (Wifi, Ethernet, Bluetooth, …).

### Services (`src/Services/`)

Protocol-level logic. One service per controller, plus a few cross-cutting ones: `NvsService` (NVS settings, especially Wi-Fi credentials), `LittleFsService` (on-device filesystem exposed over HTTP), `SdService`, `PinService`, `SystemService`, `UtilityService`. Each service implements a small `I*Service` interface declared in `src/Interfaces/`.

### Views and inputs (`src/Views/`, `src/Inputs/`, `src/Boards/`)

- `ITerminalView` is implemented by `SerialTerminalView`, `WebTerminalView`, and `CardputerTerminalView`. `main.cpp` picks one based on `TerminalTypeEnum` (Serial / WiFiAp / WiFiClient / Standalone).
- `IInput` is implemented by `SerialTerminalInput`, `WebTerminalInput`, and the board-specific `CardputerInput` / `DefaultInput` / `TEmbedInput` / `CustomInput`.
- `IDeviceView` is the on-device screen (mode list, pinout map, logic traces). `NoScreenDeviceView` is the no-LCD fallback.

### State (`src/States/GlobalState.h`)

`GlobalState` is a singleton (`GlobalState::getInstance()`) holding the current mode, terminal type, all per-protocol pin maps, baud rates, Wi-Fi credentials, etc. It also reads compile-time constants from `-D*_PIN` / `-D*_BAUD` / etc. defined per env in `platformio.ini`. `test/States/TestGlobalState.h` is the test override.

### Transformers (`src/Transformers/`)

Pure-function string/format converters: `TerminalCommandTransformer` (CLI parsing), `ArgTransformer` (string → uint8/uint32/hex), `InstructionTransformer` (bytecode), `JsonTransformer`, `WebRequestTransformer`, `PinoutTransformer`, `AtTransformer`, `Bpio2Transformer`, `InfraredRemoteTransformer`, `SubGhzTransformer`, `LoRaTransformer`, `ProfileTransformer`.

### Shells (`src/Shells/`)

Interactive, multi-step user sessions that don't fit in a single command: `SdCardShell`, `I2cEepromShell`, `SpiFlashShell`, `SpiEepromShell`, `OneWireEepromShell`, `IbuttonShell`, `SmartCardShell`, `ThreeWireEepromShell`, `UartAtShell`, `UartEmulationShell`, `ModbusShell`, `SysInfoShell`, `GuideShell`, `HelpShell`, `ProfileShell`, `MouseShell`, `CellCallShell`, `CellSmsShell`, `FmBroadcastShell`, `UsbAdapterShell`, `MeshtasticShell`, `UniversalRemoteShell`.

### Servers / web UI (`src/Servers/`, `webui/`)

`HttpServer` + `WebSocketServer` + `DnsServer` (captive portal) serve a single-page terminal UI. The HTML/CSS/JS is committed as three `.h` files (`webui/index.h`, `webui/style.h`, `webui/scripts.h`) and emitted as embedded C arrays, so updating the UI means editing the `webui/` files.

### Vendors and libs (`src/Vendors/`, `lib/`)

`src/Vendors/` holds vendored helpers (`PN532`, `i2c_sniffer`, `wifi_atks`, `MakeHex`, `RFIDInterface`). `lib/` holds platform-local dependencies that are not on the PlatformIO registry (e.g. `SX126x-Arduino`, `SmartRC-CC1101-Driver-Lib`, `IRremote`, `PN532`, `RF24`, `EEPROM_SPI_WE`, `93cx6`). Most other libraries are pulled via `lib_deps` in `platformio.ini`.

## Native test suite

`test/test_main.cpp` is the Unity entry point and `#include`s the production `.cpp` files it needs directly (not their headers), so tests can be run on a host with no ESP32 toolchain. The mirrors under `test/` exist for every layer that has unit tests (`Transformers/`, `Analyzers/`, `Managers/`, `Selectors/`, `Services/`, `Controllers/`, `Shells/`, `Enums/`, plus the `test/driver/` and `test/freertos/` shims that fake the ESP32 RMT/FreeRTOS APIs). `test/States/TestGlobalState.h` replaces `GlobalState` for tests; it must be `#include`d before any production code that pulls in `States/GlobalState.h`.

`[env:native-tests]` in `platformio.ini` sets `test_build_src = no` and `test_ignore = *` — only the explicitly-included suites in `test_main.cpp` are compiled. New tests must therefore be added to `test_main.cpp` (both the `#include` of the production `.cpp` and the `#include` of the new test file plus its `runXxxTests()` call) or they will be silently ignored.

## Adding a new command (from `CONTRIBUTING.md`)

The standard pattern is: add a `handleXxx()` method on the relevant `Controller`, which uses `terminalView` for output, `terminalInput` for keystrokes, `argTransformer`/`userInputManager` to parse and validate `TerminalCommand` args, `GlobalState` for shared state, and a `*Service` for hardware. Detailed example is in `CONTRIBUTING.md`.

## Notes for changes here

- The platform is pinned to a pioarduino Espressif32 zip in `platformio.ini`; bumping it requires re-checking every board's `lib_deps` and the partitions CSV.
- The default partition scheme is `partitions/app4M_spiffs_4M_8MB.csv` (8 MB flash) with `app4M_spiffs_12M_16MB.csv` for 16 MB flash boards. Don't change these without also touching the OTA / LittleFS docs.
- `webui/` is embedded into flash as C strings; remember to keep `index.h` small enough for the build.
- The `custom` env is the supported way to bring up a new ESP32-S3 board without touching `src/`. Edit `[env:custom]` in `platformio.ini` (pin map, display driver, input button) and the same `-DDEVICE_CUSTOM` switch in `main.cpp` will pick it up.
- This firmware can drive radios, RFID, BLE, cellular, and other hardware. The README carries a voltage and authorization warning — follow it.
