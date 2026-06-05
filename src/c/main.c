#include <pebble.h>

// ─── Layout constants ────────────────────────────────────────────────────────
#define PAD     12
#define BAR_H    4
#define TOP_Y   (BAR_H + 6)
#define STRIP_H 32

// ─── Colors ──────────────────────────────────────────────────────────────────
static GColor s_orange, s_dim, s_strip, s_green, s_yellow, s_red;

// ─── Fonts (cached at window load) ───────────────────────────────────────────
static GFont s_font_time;
static GFont s_font_small;

// ─── App state ───────────────────────────────────────────────────────────────
static Window *s_window;
static Layer  *s_canvas;

static uint8_t s_battery_pct  = 100;
static int32_t s_heart_rate   = 0;
static int     s_weather_temp = 0;
static char    s_weather_cond[8];
static bool    s_has_weather  = false;

static char s_time_buf[6];   // "HH:MM\0"
static char s_date_buf[16];  // "wed 03 jun\0"
static char s_hr_buf[10];    // "72 hr\0" or "-- hr\0"
static char s_wx_buf[16];    // "18 cldy\0" or "...\0"

static const char * const DAYS[]   = {"sun","mon","tue","wed","thu","fri","sat"};
static const char * const MONTHS[] = {"jan","feb","mar","apr","may","jun",
                                       "jul","aug","sep","oct","nov","dec"};
#define NEXT_EVENT "15:00 standup"

// ─── Buffer helpers (called by service callbacks, not in draw proc) ───────────
static void update_time_buffers(struct tm *t) {
    snprintf(s_time_buf, sizeof(s_time_buf), "%02d:%02d", t->tm_hour, t->tm_min);
    snprintf(s_date_buf, sizeof(s_date_buf), "%s %02d %s",
             DAYS[t->tm_wday], t->tm_mday, MONTHS[t->tm_mon]);
}

static void update_hr_buf(void) {
    if (s_heart_rate > 0)
        snprintf(s_hr_buf, sizeof(s_hr_buf), "%d hr", (int)s_heart_rate);
    else
        snprintf(s_hr_buf, sizeof(s_hr_buf), "-- hr");
}

static void update_wx_buf(void) {
    if (s_has_weather)
        snprintf(s_wx_buf, sizeof(s_wx_buf), "%d %s", s_weather_temp, s_weather_cond);
    else
        snprintf(s_wx_buf, sizeof(s_wx_buf), "...");
}

// ─── Canvas draw proc ────────────────────────────────────────────────────────
static void canvas_update_proc(Layer *layer, GContext *ctx) {
    GRect bounds = layer_get_bounds(layer);
    int W = bounds.size.w;
    int H = bounds.size.h;

    // Background
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_rect(ctx, bounds, 0, GCornerNone);

    // Battery bar (top 4 px, proportional width, colour-coded)
    GColor bar_col = (s_battery_pct <= 20) ? s_red :
                     (s_battery_pct <= 40) ? s_yellow : s_green;
    graphics_context_set_fill_color(ctx, bar_col);
    graphics_fill_rect(ctx, GRect(0, 0, ((int)s_battery_pct * W) / 100, BAR_H),
                       0, GCornerNone);

    // Heart rate — left, orange
    graphics_context_set_text_color(ctx, s_orange);
    graphics_draw_text(ctx, s_hr_buf, s_font_small,
                       GRect(PAD, TOP_Y, 90, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);

    // Weather — right, dim
    graphics_context_set_text_color(ctx, s_dim);
    graphics_draw_text(ctx, s_wx_buf, s_font_small,
                       GRect(W / 2, TOP_Y, W / 2 - PAD, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

    // Time — large, centred at ~44% down
    int time_h = 58;
    int time_y = (H * 44) / 100 - time_h / 2 - 8;
    graphics_context_set_text_color(ctx, GColorWhite);
    graphics_draw_text(ctx, s_time_buf, s_font_time,
                       GRect(0, time_y, W, time_h),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Date — below time, dim
    graphics_context_set_text_color(ctx, s_dim);
    graphics_draw_text(ctx, s_date_buf, s_font_small,
                       GRect(0, time_y + time_h + 4, W, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);

    // Bottom strip
    graphics_context_set_fill_color(ctx, s_strip);
    graphics_fill_rect(ctx, GRect(0, H - STRIP_H, W, STRIP_H), 0, GCornerNone);
    graphics_context_set_text_color(ctx, s_orange);
    graphics_draw_text(ctx, NEXT_EVENT, s_font_small,
                       GRect(PAD, H - STRIP_H + 7, W - PAD * 2, 24),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
}

// ─── Service callbacks ────────────────────────────────────────────────────────

static void tick_handler(struct tm *tick_time, TimeUnits units) {
    update_time_buffers(tick_time);
    layer_mark_dirty(s_canvas);
}

static void battery_handler(BatteryChargeState state) {
    s_battery_pct = state.charge_percent;
    layer_mark_dirty(s_canvas);
}

static void health_handler(HealthEventType event, void *context) {
    if (event == HealthEventHeartRateUpdate || event == HealthEventSignificantUpdate) {
        HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
        s_heart_rate = (hr > 0) ? (int32_t)hr : 0;
        update_hr_buf();
        layer_mark_dirty(s_canvas);
    }
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
        update_wx_buf();
        layer_mark_dirty(s_canvas);
    }
}

// ─── Window lifecycle ─────────────────────────────────────────────────────────

static void window_load(Window *window) {
    // Cache fonts once — never call fonts_get_system_font in the draw proc
    s_font_time  = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
    s_font_small = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

    Layer *root = window_get_root_layer(window);
    s_canvas = layer_create(layer_get_bounds(root));
    layer_set_update_proc(s_canvas, canvas_update_proc);
    layer_add_child(root, s_canvas);

    // Seed all display buffers before the first draw
    time_t now = time(NULL);
    update_time_buffers(localtime(&now));

    s_battery_pct = battery_state_service_peek().charge_percent;

    // Health peek — guard with accessibility check
    HealthServiceAccessibilityMask mask =
        health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
    if (mask & HealthServiceAccessibilityMaskAvailable) {
        HealthValue hr = health_service_peek_current_value(HealthMetricHeartRateBPM);
        s_heart_rate = (hr > 0) ? (int32_t)hr : 0;
    }
    update_hr_buf();
    update_wx_buf();
}

static void window_unload(Window *window) {
    layer_destroy(s_canvas);
}

// ─── App lifecycle ────────────────────────────────────────────────────────────

static void init(void) {
    s_orange = GColorFromRGB(255, 170,   0);
    s_dim    = GColorFromRGB(138, 138, 138);
    s_strip  = GColorFromRGB( 20,  20,  20);
    s_green  = GColorFromRGB(  0, 210, 106);
    s_yellow = GColorFromRGB(255, 170,   0);
    s_red    = GColorRed;

    s_window = window_create();
    window_set_background_color(s_window, GColorBlack);
    window_set_window_handlers(s_window, (WindowHandlers){
        .load   = window_load,
        .unload = window_unload,
    });
    window_stack_push(s_window, true);

    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
    battery_state_service_subscribe(battery_handler);
    health_service_events_subscribe(health_handler, NULL);

    app_message_register_inbox_received(inbox_received);
    app_message_open(128, 64);
}

static void deinit(void) {
    tick_timer_service_unsubscribe();
    battery_state_service_unsubscribe();
    health_service_events_unsubscribe();
    window_destroy(s_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
    return 0;
}
