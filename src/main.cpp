// Addressable RGB LED strip controller for a classic ESP32.
// Driving 2x daisy-chained WS2811 12V "dream color" pixel modules
// through a logic level shifter (see WIRING.md).
//
// - Creates its own WiFi hotspot (AP mode) — connect your phone to it.
// - Serves a small web UI at http://192.168.4.1/
// - Also exposes a JSON HTTP API so any client (Flutter app, curl, etc.)
//   can control the strip without needing to parse a web page.
// - Effects run non-blocking (millis()-based) so the web server stays
//   responsive while animations play.

#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------
// Hardware configuration — EDIT THESE to match your actual modules.
// ---------------------------------------------------------------------
#define LED_PIN      5        // ESP32 GPIO -> level shifter input (see WIRING.md)
#define NUM_LEDS     6       // 2 modules x 6 LEDs each — COUNT YOUR ACTUAL LEDs and fix this
#define LED_TYPE     WS2811   // these are WS2811 modules, not WS2812B
#define COLOR_ORDER  RGB      // WS2811 modules are typically wired RGB — flip to GRB/BRG etc. if colors look wrong

// WiFi hotspot credentials — change the password before real use.
const char* AP_SSID     = "LED-Controller";
const char* AP_PASSWORD = "ledcontrol123"; // must be 8+ chars for WPA2

// ---------------------------------------------------------------------

CRGB leds[NUM_LEDS];
WebServer server(80);

uint8_t currentBrightness = 128;
CRGB currentColor = CRGB::Red;

enum class Effect { NONE, RAINBOW, CHASE, BREATHE };
Effect currentEffect = Effect::NONE;

unsigned long lastEffectStep = 0;
uint16_t effectStep = 0;

// ---------------------------------------------------------------------
// Effect frame renderers — each draws ONE frame and returns; the main
// loop calls these on a timer instead of blocking with delay().
// ---------------------------------------------------------------------

void stepRainbow() {
  fill_rainbow(leds, NUM_LEDS, effectStep, 255 / max(1, NUM_LEDS));
  effectStep = (effectStep + 1) % 256;
  FastLED.show();
}

void stepChase() {
  fadeToBlackBy(leds, NUM_LEDS, 40);
  int pos = effectStep % NUM_LEDS;
  leds[pos] = currentColor;
  effectStep++;
  FastLED.show();
}

void stepBreathe() {
  // Triangle wave 0..255..0 over ~256 steps for a smooth pulse.
  uint8_t level = (effectStep < 128) ? (effectStep * 2) : (255 - (effectStep - 128) * 2);
  fill_solid(leds, NUM_LEDS, currentColor);
  FastLED.setBrightness(level);
  effectStep = (effectStep + 1) % 256;
  FastLED.show();
}

void runEffectFrame() {
  unsigned long now = millis();
  const unsigned long frameInterval = 30; // ms between frames (~33fps)
  if (now - lastEffectStep < frameInterval) return;
  lastEffectStep = now;

  switch (currentEffect) {
    case Effect::RAINBOW: stepRainbow(); break;
    case Effect::CHASE:   stepChase();   break;
    case Effect::BREATHE: stepBreathe(); break;
    case Effect::NONE:    break;
  }
}

void applySolidColor(CRGB color) {
  currentEffect = Effect::NONE;
  currentColor = color;
  FastLED.setBrightness(currentBrightness);
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
}

void applyOff() {
  currentEffect = Effect::NONE;
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// ---------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------

void sendJson(const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void sendError(const String& msg) {
  JsonDocument doc;
  doc["error"] = msg;
  String out;
  serializeJson(doc, out);
  server.send(400, "application/json", out);
}

void handleStatus() {
  JsonDocument doc;
  doc["numLeds"] = NUM_LEDS;
  doc["brightness"] = currentBrightness;
  doc["color"]["r"] = currentColor.r;
  doc["color"]["g"] = currentColor.g;
  doc["color"]["b"] = currentColor.b;
  const char* effectName = "none";
  switch (currentEffect) {
    case Effect::RAINBOW: effectName = "rainbow"; break;
    case Effect::CHASE:   effectName = "chase";   break;
    case Effect::BREATHE: effectName = "breathe"; break;
    default: break;
  }
  doc["effect"] = effectName;
  sendJson(doc);
}

// GET /api/color?r=255&g=0&b=0
void handleColor() {
  if (!server.hasArg("r") || !server.hasArg("g") || !server.hasArg("b")) {
    sendError("missing r/g/b args");
    return;
  }
  int r = server.arg("r").toInt();
  int g = server.arg("g").toInt();
  int b = server.arg("b").toInt();
  r = constrain(r, 0, 255);
  g = constrain(g, 0, 255);
  b = constrain(b, 0, 255);
  applySolidColor(CRGB(r, g, b));
  handleStatus();
}

// GET /api/pixel?i=0&r=255&g=0&b=0
void handlePixel() {
  if (!server.hasArg("i") || !server.hasArg("r") || !server.hasArg("g") || !server.hasArg("b")) {
    sendError("missing i/r/g/b args");
    return;
  }
  int idx = server.arg("i").toInt();
  if (idx < 0 || idx >= NUM_LEDS) {
    sendError("index out of range");
    return;
  }
  int r = constrain(server.arg("r").toInt(), 0, 255);
  int g = constrain(server.arg("g").toInt(), 0, 255);
  int b = constrain(server.arg("b").toInt(), 0, 255);
  currentEffect = Effect::NONE;
  leds[idx] = CRGB(r, g, b);
  FastLED.show();
  handleStatus();
}

// GET /api/brightness?v=128
void handleBrightness() {
  if (!server.hasArg("v")) {
    sendError("missing v arg");
    return;
  }
  int v = constrain(server.arg("v").toInt(), 0, 255);
  currentBrightness = v;
  FastLED.setBrightness(v);
  FastLED.show();
  handleStatus();
}

// GET /api/effect?name=rainbow|chase|breathe|off
void handleEffect() {
  if (!server.hasArg("name")) {
    sendError("missing name arg");
    return;
  }
  String name = server.arg("name");
  effectStep = 0;
  if (name == "rainbow") currentEffect = Effect::RAINBOW;
  else if (name == "chase") currentEffect = Effect::CHASE;
  else if (name == "breathe") currentEffect = Effect::BREATHE;
  else if (name == "off") { applyOff(); handleStatus(); return; }
  else { sendError("unknown effect"); return; }
  handleStatus();
}

// ---------------------------------------------------------------------
// Web UI — Short Circuit brand style (red/black, Anton + Poppins).
// Fonts load from Google Fonts if the client has an internet route;
// otherwise the bold system-font fallback approximates the same look.
// ---------------------------------------------------------------------

const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LED Controller</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Anton&family=Poppins:wght@300;400;500;600&display=swap');

  :root {
    --sc-red: #eb1b26;
    --sc-dark-red: #a40e16;
    --bg-primary: #0a0a0a;
    --bg-secondary: #141414;
    --card-bg: #1a1a1a;
    --card-border: #2a2a2a;
    --text-primary: #ffffff;
    --text-secondary: #e0e0e0;
    --text-muted: #999999;
  }

  * { box-sizing: border-box; }

  body {
    margin: 0;
    padding: 32px 20px 60px;
    background: var(--bg-primary);
    color: var(--text-primary);
    font-family: 'Poppins', -apple-system, 'Segoe UI', Roboto, sans-serif;
    font-weight: 400;
  }

  .wrap { max-width: 420px; margin: 0 auto; }

  h1 {
    font-family: 'Anton', 'Arial Narrow', sans-serif;
    font-weight: 400;
    font-size: 32px;
    text-transform: uppercase;
    letter-spacing: 1px;
    text-align: center;
    margin: 0 0 6px;
  }

  .accent {
    width: 56px;
    height: 3px;
    margin: 0 auto 8px;
    background: linear-gradient(to right, var(--sc-dark-red), var(--sc-red));
    border-radius: 2px;
  }

  .subtitle {
    font-weight: 300;
    font-size: 12px;
    letter-spacing: 0.08em;
    text-transform: uppercase;
    text-align: center;
    color: var(--text-muted);
    margin: 0 0 32px;
  }

  .card {
    background: var(--card-bg);
    border: 1px solid var(--card-border);
    border-radius: 12px;
    padding: 20px;
    margin-bottom: 16px;
  }

  .card-label {
    font-weight: 500;
    font-size: 13px;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    color: var(--text-secondary);
    margin: 0 0 14px;
  }

  .color-row {
    display: flex;
    align-items: center;
    gap: 14px;
  }

  input[type=color] {
    width: 56px;
    height: 56px;
    border: 2px solid var(--card-border);
    border-radius: 10px;
    background: none;
    padding: 0;
    cursor: pointer;
    flex-shrink: 0;
  }

  .apply-btn {
    flex: 1;
    padding: 15px 16px;
    border: none;
    border-radius: 10px;
    background: var(--sc-red);
    color: #ffffff;
    font-family: 'Poppins', sans-serif;
    font-weight: 600;
    font-size: 13px;
    letter-spacing: 0.04em;
    text-transform: uppercase;
    cursor: pointer;
  }

  .apply-btn:active {
    background: linear-gradient(to right, var(--sc-dark-red), var(--sc-red));
  }

  input[type=range] {
    width: 100%;
    -webkit-appearance: none;
    height: 4px;
    border-radius: 2px;
    background: var(--card-border);
    outline: none;
    margin: 4px 0 0;
  }

  input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none;
    width: 20px;
    height: 20px;
    border-radius: 50%;
    background: var(--sc-red);
    cursor: pointer;
    border: 3px solid var(--bg-primary);
    box-shadow: 0 0 0 1px var(--sc-red);
  }

  input[type=range]::-moz-range-thumb {
    width: 20px;
    height: 20px;
    border-radius: 50%;
    background: var(--sc-red);
    cursor: pointer;
    border: 3px solid var(--bg-primary);
  }

  .brightness-val {
    float: right;
    font-weight: 500;
    color: var(--text-muted);
  }

  .effects-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 10px;
  }

  .fx-btn {
    padding: 14px 10px;
    border: 1px solid var(--card-border);
    border-radius: 10px;
    background: var(--bg-secondary);
    color: var(--text-secondary);
    font-family: 'Poppins', sans-serif;
    font-weight: 500;
    font-size: 13px;
    letter-spacing: 0.03em;
    text-transform: uppercase;
    cursor: pointer;
  }

  .fx-btn.active {
    border-color: var(--sc-red);
    color: var(--sc-red);
    background: rgba(235, 27, 38, 0.08);
  }

  .fx-btn.off-btn {
    grid-column: 1 / -1;
    color: var(--text-muted);
  }
</style>
</head>
<body>
  <div class="wrap">
    <h1>LED Controller</h1>
    <div class="accent"></div>
    <p class="subtitle">Short Circuit Company</p>

    <div class="card">
      <p class="card-label">Color</p>
      <div class="color-row">
        <input type="color" id="colorPicker" value="#ff0000">
        <button class="apply-btn" onclick="sendColor()">Apply</button>
      </div>
    </div>

    <div class="card">
      <p class="card-label">Brightness <span class="brightness-val" id="brightnessVal">128</span></p>
      <input type="range" id="brightness" min="0" max="255" value="128"
             oninput="document.getElementById('brightnessVal').textContent = this.value"
             onchange="sendBrightness(this.value)">
    </div>

    <div class="card">
      <p class="card-label">Effect</p>
      <div class="effects-grid" id="effectsGrid">
        <button class="fx-btn" data-fx="rainbow" onclick="sendEffect('rainbow')">Rainbow</button>
        <button class="fx-btn" data-fx="chase" onclick="sendEffect('chase')">Chase</button>
        <button class="fx-btn" data-fx="breathe" onclick="sendEffect('breathe')">Breathe</button>
        <button class="fx-btn off-btn" data-fx="none" onclick="sendEffect('off')">Off</button>
      </div>
    </div>
  </div>

<script>
function hexToRgb(hex) {
  const v = parseInt(hex.slice(1), 16);
  return { r: (v >> 16) & 255, g: (v >> 8) & 255, b: v & 255 };
}
function rgbToHex(r, g, b) {
  return '#' + [r, g, b].map(x => x.toString(16).padStart(2, '0')).join('');
}
function highlightActive(effect) {
  document.querySelectorAll('.fx-btn').forEach(btn => {
    btn.classList.toggle('active', btn.dataset.fx === effect);
  });
}
function sendColor() {
  const hex = document.getElementById('colorPicker').value;
  const { r, g, b } = hexToRgb(hex);
  fetch(`/api/color?r=${r}&g=${g}&b=${b}`).then(refreshFromResponse);
}
function sendBrightness(v) {
  fetch(`/api/brightness?v=${v}`).then(refreshFromResponse);
}
function sendEffect(name) {
  fetch(`/api/effect?name=${name}`).then(refreshFromResponse);
}
function refreshFromResponse(res) {
  return res.json().then(applyState);
}
function applyState(state) {
  document.getElementById('brightness').value = state.brightness;
  document.getElementById('brightnessVal').textContent = state.brightness;
  document.getElementById('colorPicker').value = rgbToHex(state.color.r, state.color.g, state.color.b);
  highlightActive(state.effect === 'none' ? 'none' : state.effect);
}
fetch('/api/status').then(res => res.json()).then(applyState);
</script>
</body>
</html>
)HTML";

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleNotFound() {
  server.send(404, "application/json", "{\"error\":\"not found\"}");
}

// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(currentBrightness);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP started. Connect to WiFi \"");
  Serial.print(AP_SSID);
  Serial.println("\" and open http://192.168.4.1/");

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/color", handleColor);
  server.on("/api/pixel", handlePixel);
  server.on("/api/brightness", handleBrightness);
  server.on("/api/effect", handleEffect);
  server.onNotFound(handleNotFound);
  server.begin();
}

void loop() {
  server.handleClient();
  runEffectFrame();
}
