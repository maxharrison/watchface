# Pebble Alloy (Moddable JS) Watchface Development Skill

Use this skill when working on Pebble watchfaces written with the **Alloy/Moddable JavaScript SDK** — the modern JS-native SDK for Pebble Time 2 (emery) and Pebble Round 2 (gabbro). This is distinct from the old C SDK or Rocky.js.

---

## Platform Reference

| Platform | Shape     | Resolution | Colors |
|----------|-----------|------------|--------|
| emery    | rect      | 200 × 228  | 64     |
| gabbro   | round     | 180 × 180  | 64     |

Access at runtime: `render.width`, `render.height` (never hardcode dimensions).

---

## Project Structure

```
my-watchface/
├── package.json             # pebble metadata + npm deps
└── src/
    ├── embeddedjs/
    │   ├── main.js          # watch-side JS (runs on-device via XS engine)
    │   └── manifest.json    # Moddable module manifest
    ├── pkjs/
    │   └── index.js         # phone-side JS (runs in PebbleKit JS on phone)
    └── c/
        └── mdbl.c           # thin C boilerplate (do not modify)
```

`embeddedjs/` runs on the watch (Moddable XS engine). `pkjs/` runs on the user's phone and has full network access.

---

## package.json

```json
{
  "name": "my-watchface",
  "version": "1.0.0",
  "author": "Your Name",
  "keywords": ["pebble-app"],
  "dependencies": {
    "@moddable/pebbleproxy": "^0.1.8"
  },
  "pebble": {
    "displayName": "My Watchface",
    "uuid": "GENERATE-A-UUID-HERE",
    "sdkVersion": "3",
    "projectType": "moddable",
    "enableMultiJS": true,
    "targetPlatforms": ["emery", "gabbro"],
    "capabilities": ["location", "health", "configurable"],
    "messageKeys": [],
    "watchapp": { "watchface": true },
    "resources": { "media": [] }
  }
}
```

Capabilities: `"location"` (GPS via phone), `"health"` (heart rate, steps), `"configurable"` (settings page).

---

## manifest.json (embeddedjs/)

```json
{
  "include": ["$(MODDABLE)/examples/manifest_mod.json"],
  "modules": { "*": "./main.js" }
}
```

To add more source files: `"*": ["./main.js", "./utils.js"]`

---

## Poco Graphics — Core Drawing API

Poco is the procedural low-level renderer. Always bracket drawing with `render.begin()` / `render.end()`.

```js
import Poco from "commodetto/Poco";

const render = new Poco(screen);  // `screen` is a global

// Colors
const black = render.makeColor(0, 0, 0);
const white = render.makeColor(255, 255, 255);
const red   = render.makeColor(255, 0, 0);

// Fonts
const timeFont  = new render.Font("Bitham-Bold", 42);
const smallFont = new render.Font("Gothic-Bold", 18);

render.begin();

// Fill background
render.fillRectangle(black, 0, 0, render.width, render.height);

// Draw filled rect: (color, x, y, w, h)
render.fillRectangle(white, 0, 0, render.width, 4);

// Draw text: (text, font, color, x, y)
render.drawText("Hello", timeFont, white, 10, 20);

// Measure text width before drawing (for centering/right-align)
const w = render.getTextWidth("Hello", timeFont);
render.drawText("Hello", timeFont, white, (render.width - w) / 2 | 0, 20);

render.end();
```

### Available Built-in Fonts

- `Bitham-Bold` — large bold digital clock face (42px typical)
- `Gothic-Bold` — medium bold sans-serif (18px, 24px)
- `Gothic-Medium` — medium weight
- `Leco-26-Bold-Numbers-Am-Pm` — stylized numerals
- `RobotoCondensed-21` — condensed readable font

Font height: `timeFont.height` (integer, pixels).

### Pixel Math

Use `| 0` for integer truncation (no `Math.floor` needed): `(x / 2) | 0`

---

## Watch Events

```js
// Fires every minute (and immediately on registration — no separate init call needed)
watch.addEventListener("minutechange", (event) => {
    const now = event.date;  // Date object
    drawScreen(now);
});

// Fires every hour
watch.addEventListener("hourchange", (event) => {
    refreshWeather();
});

// Fires every day (midnight)
watch.addEventListener("daychange", (event) => { });

// App focus (watchface visible/hidden)
watch.addEventListener("focus",  () => { });
watch.addEventListener("blur",   () => { });
```

`event.date` is a standard JS `Date` object. The `minutechange` listener fires immediately upon registration, so you don't need a separate startup draw call.

---

## Sensors

### Battery

```js
import Battery from "embedded:sensor/Battery";

let batteryPercent = 100;
const battery = new Battery({
    onSample() {
        batteryPercent = this.sample().percent;  // 0–100
        drawScreen();
    }
});
batteryPercent = battery.sample().percent;  // initial sync read
```

`sample()` returns `{ percent: Number, isCharging: Boolean }`.

### Location (GPS via phone)

```js
import Location from "embedded:sensor/Location";

function requestLocation() {
    new Location({
        onSample() {
            const { latitude, longitude } = this.sample();
            this.close();  // one-shot: close after first fix
            fetchWeather(latitude, longitude);
        }
    });
}
```

Requires `"location"` in `pebble.capabilities`. Location data comes from the paired phone's GPS.

### Health — Heart Rate

```js
import Health from "embedded:sensor/Health";

let heartRate = 0;
const health = new Health({
    onSample() {
        const s = this.sample();
        heartRate = s.heartRate ?? 0;  // filtered BPM; 0 if no reading
        drawScreen();
    }
});
heartRate = health.sample().heartRate ?? 0;
```

Requires `"health"` in `pebble.capabilities`. `sample()` returns `{ heartRate, steps, activeCalories, restingCalories }`.

### Accelerometer

```js
import Accelerometer from "embedded:sensor/Accelerometer";

const accel = new Accelerometer({
    onSample() {
        const { x, y, z } = this.sample();  // values in mG
    }
});
```

### Button Input

```js
watch.addEventListener("buttondown", (event) => {
    // event.button: "up", "select", "down", "back"
    if (event.button === "select") { /* ... */ }
});
watch.addEventListener("buttonup", (event) => { });
```

---

## Networking (fetch / WebSocket)

`fetch` is available in `embeddedjs/` via the phone connection. Prefer `async/await`.

```js
async function fetchWeather(lat, lon) {
    try {
        const url = new URL("http://api.open-meteo.com/v1/forecast");
        url.search = new URLSearchParams({
            latitude: lat,
            longitude: lon,
            current: "temperature_2m,weather_code"
        });
        const res  = await fetch(url);
        const data = await res.json();
        // use data.current.temperature_2m, data.current.weather_code
    } catch (e) {
        console.log("fetch error: " + e);
    }
}
```

Network calls are proxied through the phone via `@moddable/pebbleproxy`. Always wrap in try/catch — the watch may not be connected.

---

## Storage (Persistent)

`localStorage` persists across reboots. Only stores strings; use JSON for objects.

```js
// Write
localStorage.setItem("settings", JSON.stringify({ color: "white" }));

// Read (with fallback)
const raw = localStorage.getItem("settings");
const settings = raw ? JSON.parse(raw) : { color: "white" };

// Remove
localStorage.removeItem("settings");
```

---

## pkjs — Phone-Side JS

`src/pkjs/index.js` runs on the phone with full Node.js-like access.

Minimal proxy setup (required for Moddable):
```js
const moddableProxy = require("@moddable/pebbleproxy");
Pebble.addEventListener("ready", moddableProxy.readyReceived);
Pebble.addEventListener("appmessage", moddableProxy.appMessageReceived);
```

To also add a settings page (Clay):
```js
const Clay = require("pebble-clay");
const clayConfig = require("./config.js");
const clay = new Clay(clayConfig);
```

---

## User Settings (Clay)

1. Add `"configurable"` to `pebble.capabilities`
2. Add message key names to `pebble.messageKeys` in package.json
3. Create `src/pkjs/config.js` with Clay config array
4. Read settings on watch with `localStorage` (synced automatically by Clay)

```js
// package.json
"messageKeys": ["backgroundColor", "showSeconds"]

// src/pkjs/config.js
module.exports = [
  { type: "heading", defaultValue: "My Watchface Settings" },
  { type: "toggle",  id: "showSeconds",      label: "Show Seconds",        defaultValue: false },
  { type: "color",   id: "backgroundColor",  label: "Background Color",    defaultValue: "000000" }
];
```

---

## Date & Time Formatting

```js
const DAYS   = ["sun","mon","tue","wed","thu","fri","sat"];
const MONTHS = ["jan","feb","mar","apr","may","jun",
                "jul","aug","sep","oct","nov","dec"];

const now = event.date;  // from minutechange
const hours   = String(now.getHours()).padStart(2, "0");
const minutes = String(now.getMinutes()).padStart(2, "0");
const seconds = String(now.getSeconds()).padStart(2, "0");
const dayStr  = DAYS[now.getDay()];
const dateStr = `${dayStr} ${String(now.getDate()).padStart(2,"0")} ${MONTHS[now.getMonth()]}`;
```

---

## Layout Patterns

```js
const W   = render.width;
const H   = render.height;
const pad = 12;  // consistent edge padding

// Center horizontally
const cx = ((W - render.getTextWidth(str, font)) / 2) | 0;

// Right-align
const rx = W - pad - render.getTextWidth(str, font);

// Vertical center of screen
const midY = (H / 2) | 0;

// Bottom strip (e.g. calendar bar)
const stripH = smallFont.height + 14;
render.fillRectangle(stripBg, 0, H - stripH, W, stripH);
render.drawText(label, smallFont, orange, pad, H - stripH + 7);

// Battery bar across top (proportional width)
const barW = ((batteryPercent * W) / 100) | 0;
render.fillRectangle(barColor, 0, 0, barW, 4);
```

---

## Common Gotchas

- **Always call `render.begin()` before any draw call and `render.end()` after** — skipping either corrupts the display.
- **`minutechange` fires immediately** — no need for a separate startup draw. Store the date: `if (event?.date) lastDate = event.date; const now = event?.date ?? lastDate;`
- **`Location` is one-shot** — call `this.close()` in `onSample` to avoid battery drain.
- **No floating-point-heavy math** — the XS engine handles it but keep it minimal; pre-compute where possible.
- **`| 0` for truncation** — use instead of `Math.floor()` for x/y coordinates.
- **Sensor constructors are async** — always do an initial `sensor.sample()` synchronously for the first render, then let `onSample` drive updates.
- **`fetch` requires phone connection** — always guard with try/catch; show a fallback state (e.g., `"..."`) when `weather` is null.
- **gabbro is circular** — when targeting both platforms, check `render.width === render.height` to detect round and adjust layout (avoid corners, use circular safe zone).

---

## Vibration

```js
import Vibe from "embedded:pebble/Vibe";
Vibe.vibe("short");   // "short" | "long" | "double"
```

---

## Console Logging

```js
console.log("debug: " + value);
trace("also works: " + value);  // Moddable-native trace
```

---

## TypeScript

TypeScript is supported. Use `.ts` extension, same imports. Type declarations are bundled with the SDK.

```ts
import Poco from "commodetto/Poco";
const render: Poco = new Poco(screen);
```

---

## Key External Resources

- Official dev portal: https://developer.repebble.com
- Alloy guide: https://developer.repebble.com/guides/alloy/
- Poco graphics guide: https://developer.repebble.com/guides/alloy/poco-guide/
- Storage guide: https://developer.repebble.com/guides/alloy/storage/
- Examples repo: https://github.com/Moddable-OpenSource/pebble-examples
- CloudPebble IDE: https://cloudpebble.repebble.com
- Open-Meteo weather API (free, no key): https://open-meteo.com
