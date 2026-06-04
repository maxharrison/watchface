#include "xsmc.h"

// Defined in health_relay.c; updated on every HealthEventHeartRateUpdate.
extern int32_t g_heart_rate;

void xs_heartrate_destructor(void *data) {}

void xs_heartrate_get(xsMachine *the) {
    xsmcSetInteger(xsResult, (xsIntegerValue)g_heart_rate);
}
