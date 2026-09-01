# Wiring & setup — ESP32 to 12V WS2811 LED strips

Covers wiring two 12V WS2811 addressable RGB strips to a classic ESP32 dev board, and the firmware changes needed since these strips differ from the 5V WS2812B strips this project originally targeted.

> **Full interactive wiring diagram**: rendered inline in the Claude conversation where this file was generated ("esp32_ws2811_12v_wiring"). A static ASCII version is included below for offline reference.

## Why this needs a level shifter

WS2811 reads its data line relative to its own supply voltage (VDD). The logic-high threshold is roughly 0.7 × VDD. At 12V that's ~8.4V — an ESP32 GPIO output (3.3V) is far below that and is not a reliable signal on its own. You need a **logic level shifter** between the ESP32's GPIO and the strip's DIN, converting the 3.3V signal up to 5V, which WS2811 strips reliably accept in practice.

Common shifter options: a 74HCT125 / 74HCT245 buffer chip, or a simple one-transistor (2N2222/BSS138) shifter circuit. Any of these works — pick whichever you have on hand.

## Components

| Component | Notes |
|---|---|
| ESP32 dev board | Classic ESP32 (not S3), 3.3V logic |
| WS2811 12V addressable strip × 2 | Daisy-chained, or driven from two separate GPIO pins |
| Logic level shifter | 3.3V → 5V, e.g. 74HCT125 |
| 5V supply | Powers the level shifter's VCC only (small current, can share the ESP32's 5V rail if USB-powered) |
| 12V power supply | Sized for both strips' total current draw — see note below |
| Common ground bus | Ties ESP32 GND, level shifter GND, and 12V supply GND together |

**Sizing the 12V supply**: WS2811 strips draw roughly 60 mA per pixel at full white/full brightness (varies by strip — check your product page). Multiply by total pixel count across both strips, add ~20% headroom. For long strips, inject 12V/GND at both ends rather than relying on the daisy-chain wire alone to carry it.

## Pinout

| ESP32 pin | Function | Connects to |
|---|---|---|
| GPIO 5 | Data out (3.3V logic) | Level shifter input (IN/A) |
| 5V | Level shifter power | Level shifter VCC |
| GND | Common ground | Level shifter GND **and** 12V supply GND |
| — | Level shifter output (5V logic) | Strip 1 DIN |
| — | Strip 1 DOUT | Strip 2 DIN (daisy-chain) |
| — | 12V supply + | Strip 1 V+ **and** Strip 2 V+ |
| — | 12V supply − | Strip 1 GND **and** Strip 2 GND |

## ASCII wiring diagram

```
 ESP32                 Level shifter            WS2811 strip 1        WS2811 strip 2
+--------+   GPIO5    +--------------+   5V     +--------------+  DOUT→DIN  +--------------+
|        |----------->| IN       OUT |--------->| DIN          |----------->| DIN          |
|        |            |              |          |              |            |              |
|    GND |----+       | VCC   GND    |          | V+  GND      |            | V+  GND      |
+--------+    |        +---+------+--+          +--+------+---+            +--+------+---+
              |            |      |                 |      |                  |      |
              |          5V|      |                 |      |                  |      |
              |         supply    |                 |      |                  |      |
              |            |      |                 |      |                  |      |
              +------------+------+----- common GND +------+------------------+------+
                                                      |                          |
                                              +-------+--------------------------+
                                              |            12V supply +
                                              +---------------------->
```

## Firmware changes needed

The original `main.cpp` was configured for a 5V WS2812B strip. Update these two lines for WS2811:

```cpp
#define LED_TYPE     WS2811   // was WS2812B
#define COLOR_ORDER  RGB      // WS2811 is typically RGB, not GRB — test and flip if colors look wrong
```

For two daisy-chained strips, set `NUM_LEDS` to the **combined** pixel count of both strips — FastLED addresses them as one continuous array once DOUT→DIN is chained.

If you'd rather drive the two strips independently (two GPIO pins, two separate FastLED `addLeds` calls) instead of daisy-chaining, that avoids one strip's failure taking down the other, at the cost of an extra GPIO and level-shifter channel.

## Build & flash

Unchanged from the base project:

```sh
pio run -e esp32dev -t upload
pio device monitor -e esp32dev
```

## Bring-up checklist

1. Wire and power everything with the strips **disconnected from DIN** first — verify 12V and 5V rails read correctly with a multimeter, and that all grounds are common.
2. Connect the level shifter's output to strip 1's DIN, flash the firmware, and confirm strip 1 lights up correctly before connecting strip 2.
3. If colors are swapped (e.g. red shows as blue), change `COLOR_ORDER` and reflash.
4. If pixels flicker or the first few work but later ones don't, suspect voltage drop on the 12V line — inject power at both ends of the run.
