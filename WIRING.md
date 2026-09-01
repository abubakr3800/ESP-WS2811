# Wiring & pinout — ESP32 to WS2811 12V "Dream Color" modules

Covers wiring 2x daisy-chained WS2811 12V pixel modules (Lampatronics-style "LED Soft Module") to a classic ESP32 dev board through a 74AHCT125 logic level shifter.

> **Full interactive wiring diagram**: rendered inline in the Claude conversation where this file was generated ("esp32_ws2811_full_pinout"). A static ASCII version with exact pin numbers is below for offline reference.

## Why a level shifter is required

WS2811 reads its data line relative to its own supply voltage. At 12V, the logic-high threshold is too high for a 3.3V ESP32 GPIO to reliably trigger. A **74AHCT125** buffer converts the 3.3V signal to a clean 5V signal, which WS2811 accepts reliably. Don't substitute a plain `74HC125` — its input threshold is too close to 3.3V to be reliable.

## Components

| Component | Notes |
|---|---|
| ESP32 dev board | Classic ESP32 (not S3), 3.3V logic |
| WS2811 12V pixel modules × 2 | Daisy-chained via their IN/OUT JST-SM connectors |
| 74AHCT125 level shifter | Quad buffer IC — only 1 of its 4 channels is used |
| 5V supply | Powers the level shifter's VCC only |
| 12V power supply | Sized for both modules' total current draw |
| Common ground bus | Ties ESP32, level shifter, and 12V supply grounds together |

**Sizing the 12V supply**: check the module's datasheet/listing for per-module current draw at full brightness, multiply by 2, add ~20% headroom.

## Pinout — ESP32

| ESP32 pin | Function | Connects to |
|---|---|---|
| **GPIO5** | Data out (3.3V logic) | 74AHCT125 pin 2 (1A) |
| **5V / VIN** | Level shifter power | 74AHCT125 pin 14 (VCC) — only if ESP32 is USB-powered, else use a separate 5V source |
| **GND** | Common ground | 74AHCT125 pin 7 (GND) **and** 12V supply GND |

Avoid GPIO 6–11 (internal flash — do not use), GPIO 34–39 (input-only, can't drive data), and GPIO 0/2/12/15 (boot-strapping pins). GPIO5 avoids all of these.

## Pinout — 74AHCT125 (14-pin DIP/SOIC)

Only channel 1 is used; the other three channels are left unconnected.

| Pin | Name | Connects to |
|---|---|---|
| 1 | 1OE | GND (permanently enables the output) |
| 2 | 1A | ESP32 GPIO5 (signal in) |
| 3 | 1Y | WS2811 module 1 DIN (signal out, 5V logic) |
| 7 | GND | Common ground |
| 14 | VCC | 5V supply |

A ~300–500Ω resistor in series between pin 3 (1Y) and the strip's DIN is cheap extra protection, not strictly required.

## Pinout — WS2811 modules

Each module has an IN connector and an OUT connector (3-wire: 12V / GND / DATA).

| Module 1 | Connects to |
|---|---|
| DIN | 74AHCT125 pin 3 (1Y) |
| V+ (12V) | 12V power supply + |
| GND | Common ground |
| DOUT | Module 2 DIN (daisy-chain) |

| Module 2 | Connects to |
|---|---|
| DIN | Module 1 DOUT |
| V+ (12V) | 12V power supply + |
| GND | Common ground |

## ASCII wiring diagram

```
 ESP32              74AHCT125                  WS2811 module 1        WS2811 module 2
+--------+  GPIO5  +----------------+   1Y     +--------------+ DOUT→DIN +--------------+
|        |-------->| 2:1A       3:1Y|--------->| DIN          |--------->| DIN          |
|        |         |                |          |              |          |              |
|        |         | 14:VCC  7:GND  |          | V+  GND      |          | V+  GND      |
|    GND |----+     |  |       |    |          |  |    |      |          |  |    |      |
+--------+    |     |  |5V     |    |          |  |    |      |          |  |    |      |
              |     +--+-------+----+          +--+----+------+          +--+----+------+
              |        |       |                  |    |                    |    |
              |     5V supply  |                  |    |                    |    |
              |                |                  |    |                    |    |
              +----------------+---- common GND ---+----+--------------------+----+
                                                     |                          |
                                             +-------+--------------------------+
                                             |            12V supply +
                                             +---------------------->
```

## Firmware

Already configured for this exact setup in `main.cpp`:

```cpp
#define LED_PIN      5        // GPIO5 -> 74AHCT125 pin 2
#define NUM_LEDS     12       // 2 modules x 6 LEDs each — verify against your actual modules
#define LED_TYPE     WS2811
#define COLOR_ORDER  RGB      // flip if colors look wrong after first power-up
```

## Build & flash

```sh
pio run -e esp32dev -t upload
pio device monitor -e esp32dev
```

## Bring-up checklist

1. Wire everything with the modules **disconnected from DIN** first — verify 12V and 5V rails with a multimeter, confirm all grounds are common.
2. Connect the level shifter output to module 1's DIN only, flash, confirm module 1 lights up correctly before connecting module 2.
3. If colors are swapped, change `COLOR_ORDER` and reflash.
4. If later pixels flicker or don't respond, suspect 12V voltage drop along the chain — inject 12V/GND at both ends of the run.
