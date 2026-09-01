# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project summary

Minimal addressable RGB LED strip controller for a classic ESP32 (non-S3, no native USB). It creates its own WiFi access point, serves a small web UI plus a JSON HTTP API for color/brightness/effect control, and drives a WS2812B-style strip via FastLED. Effects (rainbow, chase, breathe) run non-blocking off `millis()` so the HTTP server stays responsive.

This is a standalone sketch — a single `src/main.cpp` — not the ESP32 Bit Pirate multi-file firmware. Do not mix source trees from other ESP32 projects into `src/`; PlatformIO compiles everything it finds there and scans it for library dependencies, so stray files from another project will pull in unrelated libraries and can break the build for this target.

## Common commands

All commands are run from the repository root (`F:\ESP32-new`).

```sh
# Build
pio run -e esp32dev

# Upload to the board (adjust COM port in platformio.ini if needed)
pio run -e esp32dev -t upload

# Serial monitor (115200 baud)
pio device monitor -e esp32dev

# Clean build artifacts
pio run -e esp32dev -t clean
```

## Hardware

- **Target**: generic ESP32 dev module (`board = esp32dev` in `platformio.ini`) — a classic ESP32, not S3/S2. No native USB CDC, so nothing in this project should depend on `esp_event_base_t` / `ARDUINO_USB_CDC_*` APIs (those are S3-only).
- **Data pin**: `LED_PIN` in `main.cpp`, default GPIO5, through a ~330Ω resistor to the strip's DATA line.
- **LED count**: `NUM_LEDS` in `main.cpp`, default 30 — update to match your actual strip.
- **Chipset / color order**: `LED_TYPE` (default `WS2812B`) and `COLOR_ORDER` (default `GRB`) in `main.cpp` — flip to `RGB` if colors render swapped.
- **Power**: the LED strip must be powered from its own 5V supply, sized for the strip's current draw, NOT from the ESP32's onboard regulator (fine for a handful of LEDs at low brightness only). The strip's ground and the ESP32's ground must be tied together (common ground) even though power comes from separate sources.

## Architecture

Single-file Arduino sketch (`src/main.cpp`):

- **WiFi**: `WiFi.softAP()` starts an access point (`AP_SSID` / `AP_PASSWORD` constants) — no router/internet needed. Connect a phone or laptop directly to it, then browse to `http://192.168.4.1/`.
- **Web UI**: `INDEX_HTML` is a PROGMEM string served at `/` — color picker, brightness slider, and effect buttons that call the JSON API via `fetch()`.
- **HTTP API** (`WebServer` on port 80):
  - `GET /api/status` — current brightness/color/effect as JSON.
  - `GET /api/color?r=&g=&b=` — set a solid color.
  - `GET /api/pixel?i=&r=&g=&b=` — set one LED directly.
  - `GET /api/brightness?v=` — set global brightness (0–255).
  - `GET /api/effect?name=rainbow|chase|breathe|off` — start/stop an animated effect.
- **Effects**: `stepRainbow()`, `stepChase()`, `stepBreathe()` each render one frame; `runEffectFrame()` is called every `loop()` iteration and gates itself to ~33fps via `millis()` so it never blocks `server.handleClient()`.

## Notes for changes here

- Keep this a single-file sketch unless the project genuinely grows — there's no `lib/` or multi-module structure to preserve.
- `AP_PASSWORD` is a placeholder; change it before any real/public use.
- If you add hardware that needs native USB (CDC, HID, etc.), that requires an S3/S2 board — this classic ESP32 target does not support it.
