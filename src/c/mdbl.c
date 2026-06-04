#include <pebble.h>
#include "modules/health_relay.h"

int main(void) {
  Window *w = window_create();
  window_stack_push(w, true);

  health_relay_init();
  moddable_createMachine(NULL);
  health_relay_deinit();

  window_destroy(w);
}
