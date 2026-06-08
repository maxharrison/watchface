---
name: pebble-c
description: >-
  Pebble C SDK watchface development. Use when working on Pebble watchfaces or
  watchapps written in C with the native Pebble SDK (pebble.h) — the project has
  src/c/*.c, a wscript, and a package.json with a "pebble" block. Covers windows
  and layers, GContext drawing, system/custom fonts, the tick/battery/health/
  connection services, AppMessage + PebbleKit JS for weather, resources,
  persistent storage, and multi-platform builds. NOT for the Alloy/Moddable JS
  SDK or Rocky.js.
---

# Pebble C SDK Watchface Development Skill

Use this skill when working on Pebble watchfaces written in **C against the native Pebble SDK** (`#include <pebble.h>`). You can recognize a C-SDK project by `src/c/*.c`, a `wscript` build file, and a `package.json` containing a `"pebble"` block. This is distinct from the Alloy/Moddable **JavaScript** SDK (`src/embeddedjs/`) and from Rocky.js (`src/rocky/`).

The C SDK is the original, most capable, and lowest-overhead way to build for Pebble. It compiles to native ARM and gives you direct control over the framebuffer, services, and memory. The SDK 3.x/4.x C API has been stable since 2016 and is the API now hosted by the Pebble/Rebble revival.

---

## Platform Reference

A single app can target several hardware platforms. Pick targets in `package.json` → `pebble.targetPlatforms`.

| Platform | Watch                | Resolution | Shape | Colors | Notes                       |
|----------|----------------------|------------|-------|--------|-----------------------------|
| aplite   | Pebble / Pebble Steel| 144 × 168  | rect  | B/W    | no color, no HRM            |
| basalt   | Pebble Time          | 144 × 168  | rect  | 64     | color, microphone           |
| chalk    | Pebble Time Round    | 180 × 180  | round | 64     | circular display            |
| diorite  | Pebble 2             | 144 × 168  | rect  | B/W    | HRM, no color               |
| emery    | Pebble Time 2        | 200 × 228  | rect  | 64     | larger/sharper, HRM         |

**Never hardcode dimensions.** Read them at runtime from the layer bounds:

```c
GRect bounds = layer_get_bounds(window_get_root_layer(window));
int W = bounds.size.w;
int H = bounds.size.h;
```

At compile time you may also use `PBL_DISPLAY_WIDTH` / `PBL_DISPLAY_HEIGHT`, but prefer runtime bounds so one binary adapts to every target.

---

## Project Structure

```
my-watchface/
├── package.json          # pebble metadata + npm deps (modern format)
├── wscript               # waf build script (usually unchanged)
├── resources/            # images, custom fonts (optional)
│   ├── fonts/
│   └── images/
└── src/
    ├── c/                # watch-side native C (compiled to ARM)
    │   └── main.c
    ├── pkjs/             # phone-side PebbleKit JS (network, GPS, config)
    │   └── index.js
    └── common/           # shared JS (optional)
```

- `src/c/` runs **on the watch**. No network. Tight RAM/CPU budget.
- `src/pkjs/` runs **on the paired phone** in PebbleKit JS. Full network, geolocation, and localStorage. Talks to the watch over **AppMessage**.
- The `wscript` globs `src/c/**/*.c`, so adding `.c` files under `src/c/` requires no build-file edits.

---

## package.json

The modern package.json replaces the legacy `appinfo.json`. Key fields live under `"pebble"`:

```json
{
  "name": "my-watchface",
  "version": "1.0.0",
  "author": "Your Name",
  "keywords": ["pebble-app"],
  "dependencies": {},
  "pebble": {
    "displayName": "My Watchface",
    "uuid": "GENERATE-A-UUID-HERE",
    "sdkVersion": "3",
    "targetPlatforms": ["emery", "basalt", "chalk", "diorite", "aplite"],
    "capabilities": ["location", "health"],
    "messageKeys": ["WEATHER_TEMP_C", "WEATHER_COND"],
    "watchapp": { "watchface": true },
    "resources": { "media": [] }
  }
}
```

- `uuid` — unique per app. Generate with `uuidgen`; never reuse another app's UUID.
- `watchapp.watchface: true` — marks this as a watchface (no button-driven UI by default, shown in the watchface carousel). Omit/false for a launchable watchapp.
- `capabilities` — `"location"` (phone GPS), `"health"` (steps/HRM), `"configurable"` (settings page).
- `messageKeys` — names shared between C (`MESSAGE_KEY_<NAME>`) and JS (`"<NAME>"`) for AppMessage. The SDK assigns each a numeric key at build time.
- `resources.media` — declares fonts and images (see Resources below).

---

## wscript

The build script rarely needs editing. A typical multi-platform watchface wscript:

```python
def options(ctx):
    ctx.load('pebble_sdk')

def configure(ctx):
    ctx.load('pebble_sdk')

def build(ctx):
    ctx.load('pebble_sdk')

    binaries = []
    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'),
                      target=app_elf, bin_type='app')
        binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')
```

---

## App Lifecycle

Every C app has the same skeleton: `main()` runs `init`, enters the event loop, then `deinit` on exit.

```c
#include <pebble.h>

static Window *s_window;

static void init(void) {
    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);   // animated push

    // subscribe to services here (tick, battery, health, app_message…)
}

static void deinit(void) {
    // unsubscribe from everything, then destroy the window
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();   // blocks until the app exits; dispatches all events
    deinit();
    return 0;
}
```

`app_event_loop()` drives every callback (ticks, taps, AppMessage, button clicks). Do all teardown in `deinit` — Pebble does not garbage-collect; every `*_create` needs a matching `*_destroy`.

---

## Windows & Layers

A `Window` owns a **root `Layer`**. You attach child layers to it. Build the UI in `window_load` and tear it down in `window_unload`.

```c
static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);
    // create child layers, add with layer_add_child(root, child)
}

static void window_unload(Window *window) {
    // destroy every layer/font/bitmap created in window_load
}
```

Two ways to draw:

1. **`TextLayer` / `BitmapLayer`** — high-level widgets. Easiest for simple faces (just time + date).
2. **A bare `Layer` with a custom `update_proc`** — you draw everything yourself with `GContext`. Best for custom designs (bars, mixed text, shapes). This is the approach used by this repo's `src/c/main.c`.

You can mix both.

---

## Approach 1 — TextLayer (simplest)

```c
static TextLayer *s_time_layer;

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(root);

    s_time_layer = text_layer_create(GRect(0, bounds.size.h/2 - 28, bounds.size.w, 56));
    text_layer_set_background_color(s_time_layer, GColorClear);
    text_layer_set_text_color(s_time_layer, GColorWhite);
    text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
    text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_time_layer));
}

static void window_unload(Window *window) {
    text_layer_destroy(s_time_layer);
}
```

Update its text by calling `text_layer_set_text(s_time_layer, buf)` — point it at a **static/global buffer**, not a stack buffer (TextLayer keeps the pointer, it does not copy).

---

## Approach 2 — Custom Layer + GContext draw proc

Create one canvas layer and render the whole face in its `update_proc`. The system calls the proc whenever you `layer_mark_dirty()` it.

```c
static Layer *s_canvas;

static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    int W = bounds.size.w, H = bounds.size.h;

    // background
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // a proportional bar
    graphics_context_set_fill_color(ctx, GColorGreen);
    graphics_fill_rect(ctx, GRect(0, 0, (W * pct) / 100, 4), 0, GCornerNone);

    // text — note GRect is the bounding box, alignment positions within it
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_time_buf, s_font_time,
                       GRect(0, H/2 - 30, W, 60),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
}

static void window_load(Window *window) {
    Layer *root = window_get_root_layer(window);
    s_canvas = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);
}
```

**Golden rule:** the draw proc must be cheap and side-effect free. Do *not* call `snprintf`, `fonts_get_system_font`, `time()`, or service peeks inside it. Pre-compute strings/values in the service callbacks, store them in globals, then `layer_mark_dirty()` to trigger one redraw.

---

## GContext Drawing API

All drawing happens against a `GContext` inside an `update_proc`. State is set, then a draw call uses it.

```c
// Colors — set fill, stroke, and text color independently
graphics_context_set_fill_color(ctx, GColorWhite);
graphics_context_set_stroke_color(ctx, GColorRed);
graphics_context_set_text_color(ctx, GColorWhite);
graphics_context_set_stroke_width(ctx, 3);          // thicker lines
graphics_context_set_antialiased(ctx, true);

// Rectangles — corner_radius, and which corners to round (GCornerNone = sharp)
graphics_fill_rect(ctx, GRect(x, y, w, h), 0, GCornerNone);
graphics_fill_rect(ctx, GRect(x, y, w, h), 6, GCornersAll);  // rounded
graphics_draw_rect(ctx, GRect(x, y, w, h));                  // outline only

// Lines, circles, pixels
graphics_draw_line(ctx, GPoint(x1, y1), GPoint(x2, y2));
graphics_fill_circle(ctx, GPoint(cx, cy), radius);
graphics_draw_circle(ctx, GPoint(cx, cy), radius);

// Text — (ctx, text, font, box, overflow, alignment, NULL)
graphics_draw_text(ctx, "12:00", font, GRect(0, 0, W, 50),
                   GTextOverflowModeTrailingEllipsis,
                   GTextAlignmentCenter, NULL);
```

### Colors

`GColor` is an 8-bit ARGB value (6-bit color on color watches, dithered to B/W on aplite/diorite).

```c
GColor c1 = GColorWhite;                  // named constants
GColor c2 = GColorOrange;
GColor c3 = GColorFromRGB(255, 170, 0);   // arbitrary RGB (snapped to 64-color palette)
```

Named colors include `GColorBlack`, `GColorWhite`, `GColorRed`, `GColorGreen`, `GColorBlue`, `GColorOrange`, `GColorYellow`, `GColorCyan`, `GColorMagenta`, `GColorClear`, and many more. On B/W platforms colors auto-reduce, but use `PBL_IF_COLOR_ELSE` for intentional fallbacks (see Multi-Platform).

### Measuring text

To center or right-align text precisely, measure it first:

```c
GSize sz = graphics_text_layout_get_content_size(
    s_buf, font, GRect(0, 0, W, 100),
    GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
int x = (W - sz.w) / 2;
```

Often it's simpler to draw into a full-width box and let `GTextAlignmentCenter`/`Right` position it.

---

## Fonts

### System fonts

`fonts_get_system_font(key)` returns a `GFont` for a built-in font. These are free (no resource cost). Common keys:

- Time/large: `FONT_KEY_BITHAM_42_BOLD`, `FONT_KEY_BITHAM_42_LIGHT`, `FONT_KEY_BITHAM_34_MEDIUM_NUMBERS`
- Numeric/LECO: `FONT_KEY_LECO_42_NUMBERS`, `FONT_KEY_LECO_36_BOLD_NUMBERS`, `FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM`
- Body: `FONT_KEY_GOTHIC_28_BOLD`, `FONT_KEY_GOTHIC_24_BOLD`, `FONT_KEY_GOTHIC_18_BOLD`, `FONT_KEY_GOTHIC_14`
- Other: `FONT_KEY_ROBOTO_CONDENSED_21`, `FONT_KEY_DROID_SERIF_28_BOLD`

**Cache the GFont once** (e.g. in `window_load`), never call `fonts_get_system_font` inside a draw proc:

```c
static GFont s_font_time;
// in window_load:
s_font_time = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
```

### Custom fonts (TrueType)

1. Drop a `.ttf` in `resources/fonts/`.
2. Declare it in `package.json` (the size is baked into the resource — append it to the name):

```json
"resources": {
  "media": [
    { "type": "font", "name": "FONT_PERFECT_DOS_48", "file": "fonts/perfect-dos.ttf" }
  ]
}
```

3. Load and free it (custom fonts must be destroyed; system fonts must not):

```c
static GFont s_custom;
// window_load:
s_custom = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_PERFECT_DOS_48));
// window_unload:
fonts_unload_custom_font(s_custom);
```

The `name` field becomes `RESOURCE_ID_<name>`. To support multiple sizes, declare one resource per size (`FONT_PERFECT_DOS_20`, `FONT_PERFECT_DOS_48`, …).

---

## Time & Date

Use the C standard library `struct tm` plus Pebble helpers. Respect the user's 12/24h setting with `clock_is_24h_style()`.

```c
static char s_time_buf[8];

static void update_time(struct tm *t) {
    // strftime respects locale; %H = 24h, %I = 12h
    strftime(s_time_buf, sizeof(s_time_buf),
             clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
}

// at startup, seed before the first draw:
time_t now = time(NULL);
update_time(localtime(&now));
```

Pebble provides the day-of-week and month in `struct tm` (`tm_wday`, `tm_mon`, `tm_mday`). For lowercase/abbreviated custom formatting, index your own arrays:

```c
static const char * const DAYS[]   = {"sun","mon","tue","wed","thu","fri","sat"};
static const char * const MONTHS[] = {"jan","feb","mar","apr","may","jun",
                                       "jul","aug","sep","oct","nov","dec"};
snprintf(s_date_buf, sizeof(s_date_buf), "%s %02d %s",
         DAYS[t->tm_wday], t->tm_mday, MONTHS[t->tm_mon]);
```

---

## Services

Subscribe in `init`, **unsubscribe in `deinit`**. Each callback updates a global and marks the layer dirty.

### Tick Timer (clock)

```c
static void tick_handler(struct tm *tick_time, TimeUnits units) {
    update_time(tick_time);
    layer_mark_dirty(s_canvas);
}
// init:
tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);   // SECOND_UNIT drains battery
// deinit:
tick_timer_service_unsubscribe();
```

Use `MINUTE_UNIT` for a normal clock; `SECOND_UNIT` only if you genuinely show seconds (it costs significant battery). You can OR units (`MINUTE_UNIT | DAY_UNIT`).

### Battery

```c
static void battery_handler(BatteryChargeState state) {
    s_battery_pct = state.charge_percent;   // 0–100, multiples of 10
    // state.is_charging, state.is_plugged also available
    layer_mark_dirty(s_canvas);
}
// init:
battery_state_service_subscribe(battery_handler);
s_battery_pct = battery_state_service_peek().charge_percent;  // initial value
// deinit:
battery_state_service_unsubscribe();
```

### Connection / Bluetooth

```c
static void connection_handler(bool connected) {
    if (!connected) vibes_double_pulse();   // alert on disconnect
    layer_mark_dirty(s_canvas);
}
// init:
connection_service_subscribe((ConnectionHandlers){
    .pebble_app_connection_handler = connection_handler
});
bool connected = connection_service_peek_pebble_app_connection();
// deinit:
connection_service_unsubscribe();
```

### Health (steps & heart rate)

Requires `"health"` capability. **Guard with `PBL_API_EXISTS` / accessibility checks** — aplite and basalt have no HRM.

```c
static void health_handler(HealthEventType event, void *context) {
    if (event == HealthEventHeartRateUpdate ||
        event == HealthEventSignificantUpdate) {
        HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
        s_heart_rate = (hr > 0) ? (int)hr : 0;
        layer_mark_dirty(s_canvas);
    }
}
// init:
health_service_events_subscribe(health_handler, NULL);

// startup peek, guarded:
HealthServiceAccessibilityMask mask =
    health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_heart_rate = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
}
// deinit:
health_service_events_unsubscribe();
```

Steps: `health_service_sum_today(HealthMetricStepCount)`. Wrap HRM-specific code in `#if PBL_API_EXISTS(health_service_peek_current_value)` for compile-time safety on old SDKs.

### Accelerometer / tap

```c
static void tap_handler(AccelAxisType axis, int32_t direction) { /* … */ }
accel_tap_service_subscribe(tap_handler);   // shake/flick gestures
```

---

## Weather: AppMessage (C) + PebbleKit JS (phone)

The watch has no network. The **phone-side JS** fetches data and sends it to the watch over AppMessage using shared `messageKeys`.

### C side (watch) — receive

```c
static void inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *temp_t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_C);
    Tuple *cond_t = dict_find(iter, MESSAGE_KEY_WEATHER_COND);

    if (temp_t) s_weather_temp = (int)temp_t->value->int32;
    if (cond_t) snprintf(s_weather_cond, sizeof(s_weather_cond),
                         "%s", cond_t->value->cstring);
    if (temp_t || cond_t) layer_mark_dirty(s_canvas);
}
// init:
app_message_register_inbox_received(inbox_received);
app_message_open(128, 64);   // inbox, outbox buffer sizes in bytes
```

`MESSAGE_KEY_<NAME>` constants are generated from `package.json` → `messageKeys`. `Tuple->value` is a union: `->int32`, `->uint32`, `->cstring`, etc. — read the type you sent.

### To send from the watch (optional)

```c
DictionaryIterator *out;
app_message_outbox_begin(&out);
dict_write_int32(out, MESSAGE_KEY_SOME_KEY, 42);
app_message_outbox_send();
```

### Phone side (`src/pkjs/index.js`)

Standard PebbleKit JS. Fetch over HTTP, then `Pebble.sendAppMessage` with the same key names:

```js
function fetchWeather(lat, lon) {
  var url = 'https://api.open-meteo.com/v1/forecast?latitude=' + lat +
            '&longitude=' + lon + '&current=temperature_2m,weather_code';
  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    var data = JSON.parse(this.responseText);
    Pebble.sendAppMessage({
      'WEATHER_TEMP_C': Math.round(data.current.temperature_2m),
      'WEATHER_COND': shortConditions(data.current.weather_code)
    });
  };
  xhr.open('GET', url);
  xhr.send();
}

Pebble.addEventListener('ready', function () {
  navigator.geolocation.getCurrentPosition(
    function (pos) { fetchWeather(pos.coords.latitude, pos.coords.longitude); },
    function (err) { console.log('Location error: ' + err.message); },
    { timeout: 15000, maximumAge: 300000 }
  );
});
```

`navigator.geolocation` requires the `"location"` capability. Numbers sent as JS numbers arrive as `int32` in C; strings arrive as `cstring`. Keep C string buffers large enough for the longest value plus the NUL terminator.

---

## Resources (images)

Declare bitmaps in `package.json`, then load/draw them. Prefer PNG; the SDK converts to Pebble's internal format.

```json
"resources": {
  "media": [
    { "type": "bitmap", "name": "IMAGE_BACKGROUND", "file": "images/bg.png" }
  ]
}
```

```c
static GBitmap *s_bg;
static BitmapLayer *s_bg_layer;
// window_load:
s_bg = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BACKGROUND);
s_bg_layer = bitmap_layer_create(bounds);
bitmap_layer_set_bitmap(s_bg_layer, s_bg);
layer_add_child(root, bitmap_layer_get_layer(s_bg_layer));
// window_unload:
bitmap_layer_destroy(s_bg_layer);
gbitmap_destroy(s_bg);
```

Or draw a bitmap directly in a `GContext`: `graphics_draw_bitmap_in_rect(ctx, s_bg, GRect(...))`. To save flash, use the `~color`/`~bw` or `~rect`/`~round` filename suffixes for per-platform asset variants.

---

## Persistent Storage

Key/value storage that survives reboots and app updates. Keys are `uint32_t` you choose.

```c
#define KEY_BG_COLOR 1

// write (typically when settings change)
persist_write_int(KEY_BG_COLOR, 0xFFAA00);
persist_write_string(KEY_NAME, "Max");
persist_write_bool(KEY_SECONDS, true);

// read with a default if not yet stored
int color = persist_exists(KEY_BG_COLOR)
              ? persist_read_int(KEY_BG_COLOR) : 0x000000;
```

Settings from a Clay/config page arrive via AppMessage; persist them in `inbox_received` so they're available before the next phone connection. Storage is ~4 KB per app — store compactly.

---

## Buttons (watchapps only)

Watchfaces don't receive button events (up/down scroll the carousel, select opens the menu). For a **watchapp** (`watchapp.watchface: false`), provide a click config:

```c
static void select_click(ClickRecognizerRef recognizer, void *context) { /* … */ }

static void click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click);
    window_single_click_subscribe(BUTTON_ID_UP, up_click);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click);
}
// in init, after creating the window:
window_set_click_config_provider(s_window, click_config_provider);
```

---

## Multi-Platform Code

Branch at compile time with `PBL_*` macros so one source builds for every target:

```c
// capability branches return one of two values
GColor accent = PBL_IF_COLOR_ELSE(GColorOrange, GColorWhite);
int    inset  = PBL_IF_ROUND_ELSE(18, 8);      // round needs more edge margin

// conditional compilation
#if defined(PBL_COLOR)
    // color-only code
#endif
#if defined(PBL_ROUND)
    // chalk-specific layout
#endif
#if defined(PBL_HEALTH)
    // HRM/steps code (diorite, emery, Pebble Time 2)
#endif
```

Useful macros: `PBL_IF_COLOR_ELSE(a,b)`, `PBL_IF_ROUND_ELSE(a,b)`, `PBL_IF_RECT_ELSE(a,b)`, `PBL_IF_BW_ELSE(a,b)`, `PBL_IF_HEALTH_ELSE(a,b)`, and the defines `PBL_COLOR`, `PBL_BW`, `PBL_ROUND`, `PBL_RECT`, `PBL_HEALTH`, `PBL_DISPLAY_WIDTH`, `PBL_DISPLAY_HEIGHT`. For round (chalk), keep content inside a circular safe zone and avoid corners.

---

## Memory Management

There is **no GC**. Every create has a matching destroy, and you should pair them across `window_load`/`window_unload` (UI) and `init`/`deinit` (app/services).

| Create                          | Destroy                          |
|---------------------------------|----------------------------------|
| `window_create()`               | `window_destroy()`               |
| `layer_create()`                | `layer_destroy()`                |
| `text_layer_create()`           | `text_layer_destroy()`           |
| `bitmap_layer_create()`         | `bitmap_layer_destroy()`         |
| `gbitmap_create_with_resource()`| `gbitmap_destroy()`              |
| `fonts_load_custom_font()`      | `fonts_unload_custom_font()`     |
| `*_service_subscribe()`         | `*_service_unsubscribe()`        |

System fonts (`fonts_get_system_font`) are **not** destroyed. App RAM is tight (~24 KB on aplite, more on color platforms); avoid large stack buffers and leaks.

---

## Common Gotchas

- **Never `snprintf`/`fonts_get_*`/`time()` inside a draw proc.** Compute in callbacks, store in globals, then `layer_mark_dirty`.
- **Seed buffers before the first draw.** In `window_load`, peek battery/health and format time/date strings so the first paint isn't blank or garbage.
- **`text_layer_set_text` does not copy** — pass a static/global buffer, never a local.
- **`MINUTE_UNIT`, not `SECOND_UNIT`** unless you actually display seconds; second ticks roughly double power draw.
- **Match buffer sizes to content** including the NUL: `"HH:MM"` needs `char[6]`. Overflow silently corrupts adjacent statics.
- **Unsubscribe every service in `deinit`** and destroy in reverse creation order.
- **Guard HRM/health** with accessibility masks or `PBL_API_EXISTS` — not every platform has the sensor.
- **AppMessage may fail** (watch disconnected). Show a fallback (`"..."`) until data arrives; don't assume `dict_find` returns non-NULL.
- **Don't hardcode 144×168.** Read `layer_get_bounds` so emery (200×228) and chalk (180×180) render correctly.

---

## Performance Tips

- **Cache fonts and colors** at `window_load` / `init` scope — never construct per frame.
- **One dirty mark per event.** Multiple `layer_mark_dirty` calls in the same callback still cause a single redraw, but avoid redundant work in callbacks.
- **Split layers for partial redraws.** If only the time changes every minute but a heavy background is static, put them on separate layers so marking the time layer dirty doesn't repaint the background.
- **Stop second-granularity work** — prefer `MINUTE_UNIT`; if you need a seconds hand, unsubscribe seconds when the face isn't visible.
- **Throttle weather** — fetch on an hourly tick or `app_message`-driven schedule, not every minute.
- **Keep the draw proc O(screen).** Pre-format all strings; the proc should only set colors and call graphics primitives.

---

## Building & Installing

The Pebble tool (CLI) builds and installs to the emulator or a real watch:

```bash
pebble build                       # compile for all targetPlatforms
pebble install --emulator emery    # run in the emulator
pebble install --phone 192.168.x.x # sideload to a watch via the phone app
pebble logs --emulator emery       # view APP_LOG / console.log output
pebble clean                       # wipe build artifacts
```

`pebble build` produces a `.pbw` bundle (the installable package) in `build/`. Use `APP_LOG(APP_LOG_LEVEL_DEBUG, "x = %d", x)` in C and `console.log` in pkjs for debugging.

---

## Key External Resources

- C SDK docs: https://developer.repebble.com/docs/c/
- Watchface tutorial (C): https://developer.repebble.com/tutorials/watchface-tutorial/part1/
- Building for every Pebble: https://developer.repebble.com/guides/best-practices/building-for-every-pebble/
- Tutorial source: https://github.com/pebble-examples/watchface-tutorial
- Examples org: https://github.com/pebble-examples
- CloudPebble IDE: https://cloudpebble.repebble.com
- Open-Meteo weather API (free, no key): https://open-meteo.com
