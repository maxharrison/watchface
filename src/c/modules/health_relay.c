#include "health_relay.h"
#include <pebble.h>
#include <message_keys.auto.h>

// Reads heart rate from HealthService and sends it to the phone via AppMessage.
// PKJS receives it and forwards it back to the watch so Alloy JS can read it
// via the Message module.
//
// Data flow:
//   C HealthService -> AppMessage to phone -> PKJS -> AppMessage to watch -> Alloy Message

static AppTimer *s_retry_timer = NULL;
static AppTimer *s_startup_timer = NULL;

static void schedule_retry(uint32_t ms);

static void send_health_snapshot(void) {
  int32_t heart_rate = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);

  DictionaryIterator *iter = NULL;
  AppMessageResult result = app_message_outbox_begin(&iter);

  if (result != APP_MSG_OK) {
    // Outbox busy (prior send in flight) — retry after it clears.
    APP_LOG(APP_LOG_LEVEL_WARNING, "RELAY: outbox_begin failed: %d", (int)result);
    schedule_retry(2000);
    return;
  }

  dict_write_int32(iter, MESSAGE_KEY_HEART_RATE_BPM, heart_rate);
  result = app_message_outbox_send();

  // Do NOT retry here on failure: outbox_begin already claimed the outbox, so
  // calling outbox_begin again immediately returns APP_MSG_BUSY. The
  // outbox_failed callback fires once the system has released the transaction,
  // which is the correct place to schedule a retry.
  if (result != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "RELAY: outbox_send failed: %d", (int)result);
    return;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "RELAY: sent bpm=%ld", (long)heart_rate);
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "RELAY: outbox failed: %d (retry)", (int)reason);
  schedule_retry(2000);
}

static void retry_timer_handler(void *context) {
  s_retry_timer = NULL;
  send_health_snapshot();
}

static void schedule_retry(uint32_t ms) {
  if (s_retry_timer) return;
  s_retry_timer = app_timer_register(ms, retry_timer_handler, NULL);
}

static void startup_timer_handler(void *context) {
  s_startup_timer = NULL;
  send_health_snapshot();
}

static void health_event_handler(HealthEventType type, void *context) {
  if (type == HealthEventHeartRateUpdate) {
    send_health_snapshot();
  }
}

void health_relay_init(void) {
  app_message_register_outbox_failed(outbox_failed_handler);
  health_service_events_subscribe(health_event_handler, NULL);
  // Defer the first send so Alloy has time to open AppMessage.
  s_startup_timer = app_timer_register(1500, startup_timer_handler, NULL);
}

void health_relay_deinit(void) {
  app_message_register_outbox_failed(NULL);
  health_service_events_unsubscribe();
  if (s_startup_timer) { app_timer_cancel(s_startup_timer); s_startup_timer = NULL; }
  if (s_retry_timer)   { app_timer_cancel(s_retry_timer);   s_retry_timer   = NULL; }
}
