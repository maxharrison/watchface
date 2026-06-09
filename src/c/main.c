#include <pebble.h>

// ─── Editorial layout ────────────────────────────────────────────────────────
// A left-aligned, monochrome face on black:
//   • a thin battery hairline across the very top (width = charge %)
//   • a small letter-spaced date "eyebrow"
//   • a huge two-tone time — bright hours, dimmed minutes
//   • a single meta line: "72 BPM · 18° CLDY"
//   • a bottom row: next event (white) + countdown (grey)
//   • a thin progress bar that fills as the event approaches
//
// The Pebble 64-colour palette only offers black / dark-grey / light-grey /
// white per the mockup's grey ramp, so secondary text uses light-grey and the
// dim minutes / progress track use dark-grey.

#define PAD      10
#define BAR_H     4        // battery hairline thickness
#define EYE_Y    12
#define TIME_Y   38
#define TIME_H   56
#define PROG_H    3
#define EVENT_H  22

// Next event (no calendar integration yet — a fixed daily time). The countdown
// and progress bar are computed live from the current time.
#define EVENT_NAME      "standup"
#define EVENT_MIN_OF_DAY (17 * 60)   // 17:00
#define EVENT_WINDOW_MIN 180         // bar starts filling 3h out

// ─── Colours (cached at init) ─────────────────────────────────────────────────
static GColor s_white;   // hours, battery, event name, progress fill
static GColor s_grey;    // eyebrow, meta, countdown   (light grey)
static GColor s_dim;     // minutes, progress track     (dark grey)

// ─── Fonts (cached at window load) ────────────────────────────────────────────
static GFont s_font_time;   // huge numerics
static GFont s_font_body;   // meta + event
static GFont s_font_eye;    // small date eyebrow

// ─── App state ────────────────────────────────────────────────────────────────
static Window *s_window;
static Layer  *s_canvas;

static uint8_t s_battery_pct  = 100;
static int32_t s_heart_rate   = 0;
static int     s_weather_temp = 0;
static char    s_weather_cond[8];
static bool    s_has_weather  = false;
static int     s_progress     = 0;   // 0–100 toward the next event

static char s_hh_buf[4];     // "16\0"
static char s_mm_buf[4];     // ":05\0"
static int  s_hh_w  = 0;     // measured pixel width of the hours, for the colon
static char s_date_buf[16];  // "TUE 09 JUN\0"
static char s_meta_buf[32];  // "72 BPM · 18° CLDY\0"
static char s_count_buf[12]; // "in 55m\0" / "in 2h 5m\0"

static const char * const DAYS[]   = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
static const char * const MONTHS[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                       "JUL","AUG","SEP","OCT","NOV","DEC"};

// ─── Buffer helpers (run in service callbacks, never in the draw proc) ─────────

static void update_time_buffers(struct tm *t) {
    int hour = t->tm_hour;
    if (clock_is_24h_style()) {
        snprintf(s_hh_buf, sizeof(s_hh_buf), "%02d", hour);
    } else {
        hour %= 12;
        if (hour == 0) hour = 12;
        snprintf(s_hh_buf, sizeof(s_hh_buf), "%d", hour);
    }
    snprintf(s_mm_buf, sizeof(s_mm_buf), ":%02d", t->tm_min);

    snprintf(s_date_buf, sizeof(s_date_buf), "%s %02d %s",
             DAYS[t->tm_wday], t->tm_mday, MONTHS[t->tm_mon]);

    // Pre-measure the hours so the draw proc can butt the dim minutes up
    // against them (the font is cached before the first seed call).
    if (s_font_time) {
        GSize z = graphics_text_layout_get_content_size(
            s_hh_buf, s_font_time, GRect(0, 0, 200, TIME_H),
            GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
        s_hh_w = z.w;
    }
}

// Read whatever heart rate the system already has — peeked once per minute on
// the tick rather than via a sensor subscription, to avoid extra redraws and
// keep the HRM at its battery-friendly default rate.
static void peek_heart_rate(void) {
    HealthServiceAccessibilityMask mask =
        health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
    if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
        s_heart_rate = (hr > 0) ? (int32_t)hr : 0;
    } else {
        s_heart_rate = 0;
    }
}

// "72 BPM · 18° CLDY"  (weather half omitted until it arrives)
static void build_meta(void) {
    char wx[18] = "";
    if (s_has_weather) {
        char cond_up[8];
        int i = 0;
        for (; s_weather_cond[i] && i < (int)sizeof(cond_up) - 1; i++) {
            char c = s_weather_cond[i];
            cond_up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        cond_up[i] = '\0';
        snprintf(wx, sizeof(wx), " · %d° %s", s_weather_temp, cond_up);
    }
    if (s_heart_rate > 0)
        snprintf(s_meta_buf, sizeof(s_meta_buf), "%d BPM%s", (int)s_heart_rate, wx);
    else
        snprintf(s_meta_buf, sizeof(s_meta_buf), "-- BPM%s", wx);
}

// Countdown + progress toward the next occurrence of the event.
static void update_event(struct tm *t) {
    int now = t->tm_hour * 60 + t->tm_min;
    int left = EVENT_MIN_OF_DAY - now;
    if (left <= 0) left += 24 * 60;   // already passed today → tomorrow

    int h = left / 60, m = left % 60;
    if (h > 0 && m > 0) snprintf(s_count_buf, sizeof(s_count_buf), "in %dh %dm", h, m);
    else if (h > 0)     snprintf(s_count_buf, sizeof(s_count_buf), "in %dh", h);
    else                snprintf(s_count_buf, sizeof(s_count_buf), "in %dm", m);

    s_progress = (left >= EVENT_WINDOW_MIN)
                 ? 0
                 : (EVENT_WINDOW_MIN - left) * 100 / EVENT_WINDOW_MIN;
}

// ─── Canvas draw proc ──────────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    int W = bounds.size.w;
    int H = bounds.size.h;
    int inner = W - PAD * 2;

    // Background
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // Battery hairline — full-bleed across the top, width = charge %
    graphics_context_set_fill_color(ctx, s_white);
    graphics_fill_rect(ctx, GRect(0, 0, (int)s_battery_pct * W / 100, BAR_H),
                       0, GCornerNone);

    // Date eyebrow
    graphics_context_set_text_color(ctx, s_grey);
    graphics_draw_text(ctx, s_date_buf, s_font_eye,
                       GRect(PAD, EYE_Y, inner, 18),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Time — bright hours, then dim minutes butted against them
    graphics_context_set_text_color(ctx, s_white);
    graphics_draw_text(ctx, s_hh_buf, s_font_time,
                       GRect(PAD, TIME_Y, W - PAD, TIME_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, s_dim);
    graphics_draw_text(ctx, s_mm_buf, s_font_time,
                       GRect(PAD + s_hh_w, TIME_Y, W - PAD, TIME_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Meta — heart rate · weather
    graphics_context_set_text_color(ctx, s_grey);
    graphics_draw_text(ctx, s_meta_buf, s_font_body,
                       GRect(PAD, TIME_Y + TIME_H + 8, inner, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Bottom row — event name (left, white) + countdown (right, grey)
    int prog_y  = H - 12;
    int event_y = prog_y - 6 - EVENT_H;

    graphics_context_set_text_color(ctx, s_white);
    graphics_draw_text(ctx, EVENT_NAME, s_font_body,
                       GRect(PAD, event_y, inner, EVENT_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
    graphics_context_set_text_color(ctx, s_grey);
    graphics_draw_text(ctx, s_count_buf, s_font_body,
                       GRect(PAD, event_y, inner, EVENT_H),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

    // Progress bar — dark track with a white fill
    graphics_context_set_fill_color(ctx, s_dim);
    graphics_fill_rect(ctx, GRect(PAD, prog_y, inner, PROG_H), 0, GCornerNone);
    graphics_context_set_fill_color(ctx, s_white);
    graphics_fill_rect(ctx, GRect(PAD, prog_y, inner * s_progress / 100, PROG_H),
                       0, GCornerNone);
}

// ─── Service callbacks ──────────────────────────────────────────────────────────

static void tick_handler(struct tm *tick_time, TimeUnits units) {
    update_time_buffers(tick_time);
    update_event(tick_time);
    peek_heart_rate();
    build_meta();
    layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState state) {
    if (state.charge_percent == s_battery_pct) return;
    s_battery_pct = state.charge_percent;
    layer_mark_dirty(s_canvas);
}

static void inbox_received(DictionaryIterator *iter, void *context) {
    Tuple *temp_t = dict_find(iter, MESSAGE_KEY_WEATHER_TEMP_C);
    Tuple *cond_t = dict_find(iter, MESSAGE_KEY_WEATHER_COND);

    if (temp_t) s_weather_temp = (int)temp_t->value->int32;
    if (cond_t) {
        snprintf(s_weather_cond, sizeof(s_weather_cond), "%s", cond_t->value->cstring);
        s_has_weather = true;
    }
    if (temp_t || cond_t) {
        build_meta();
        layer_mark_dirty(s_canvas);
    }
}

// ─── Window lifecycle ─────────────────────────────────────────────────────────

static void window_load(Window *window) {
    // Cache fonts once — never call fonts_get_system_font in the draw proc.
    s_font_time = fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49);
    s_font_body = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    s_font_eye  = fonts_get_system_font(FONT_KEY_GOTHIC_14);

    Layer *root = window_get_root_layer(window);
    s_canvas = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);

    // Seed every display buffer before the first paint.
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    update_time_buffers(t);
    update_event(t);

    s_battery_pct = battery_state_service_peek().charge_percent;
    peek_heart_rate();
    build_meta();
}

static void window_unload(Window *window) {
    layer_destroy(s_canvas);
}

// ─── App lifecycle ──────────────────────────────────────────────────────────────

static void init(void) {
    s_white = GColorWhite;
    s_grey  = GColorLightGray;
    s_dim   = GColorDarkGray;

    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_handler);

    app_message_register_inbox_received(inbox_received);
    app_message_open(128, 64);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    health_service_set_heart_rate_sample_period(0);
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
