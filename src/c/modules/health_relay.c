#include "health_relay.h"
#include <pebble.h>

// Exposed to heartrate.c (native Moddable module) via extern.
// Updated on every HealthEventHeartRateUpdate; read by JS on each redraw.
int32_t g_heart_rate = 0;

static void health_event_handler(HealthEventType type, void *context) {
    if (type == HealthEventHeartRateUpdate) {
        g_heart_rate = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);
    }
}

void health_relay_init(void) {
    g_heart_rate = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);
    health_service_events_subscribe(health_event_handler, NULL);
}

void health_relay_deinit(void) {
    health_service_events_unsubscribe();
}
