/*
 * Native XS module for Pebble health / heart-rate sensor.
 * Mirrors the pattern used by pebble-battery.c in the Moddable SDK.
 */

#include "xsmc.h"
#include "xsHost.h"
#include "mc.xs.h"
#include "builtinCommon.h"
#include <pebble.h>

typedef struct {
	xsMachine	*the;
	xsSlot		 obj;
	xsSlot		*onSample;
	int32_t		 bpm;
	uint8_t		 haveSample;
} PebbleHealthRecord, *PebbleHealth;

static PebbleHealth s_health = NULL;

static void healthEvent(HealthEventType type, void *context);

void xs_health_destructor(void *data)
{
	PebbleHealth ph = data;
	if (!ph) return;
	health_service_events_unsubscribe();
	s_health = NULL;
	c_free(ph);
}

void xs_health_mark(xsMachine *the, void *it, xsMarkRoot markRoot)
{
	PebbleHealth ph = it;
	if (ph->onSample)
		(*markRoot)(the, ph->onSample);
}

static const xsHostHooks xsHealthHooks = {
	xs_health_destructor,
	xs_health_mark,
	NULL
};

void xs_health(xsMachine *the)
{
	PebbleHealth ph;
	if (s_health)
		xsUnknownError("only one");
	xsSlot *onSample = builtinGetCallback(the, xsID_onSample);
	builtinInitializeTarget(the);
	ph = c_calloc(1, sizeof(PebbleHealthRecord));
	if (!ph)
		xsRangeError("no memory");
	ph->obj = xsThis;
	xsRemember(ph->obj);
	ph->the = the;
	ph->onSample = onSample;
	// grab an initial reading synchronously
	ph->bpm = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);
	ph->haveSample = 1;
	s_health = ph;
	xsmcSetHostData(xsThis, ph);
	xsSetHostHooks(xsThis, (xsHostHooks *)&xsHealthHooks);
	if (onSample)
		health_service_events_subscribe(healthEvent, NULL);
}

void xs_health_close(xsMachine *the)
{
	PebbleHealth ph = xsmcGetHostData(xsThis);
	if (ph && xsmcGetHostDataValidate(xsThis, (void *)&xsHealthHooks)) {
		xsForget(ph->obj);
		xs_health_destructor(ph);
		xsmcSetHostData(xsThis, NULL);
		xsmcSetHostDestructor(xsThis, NULL);
	}
}

void xs_health_sample(xsMachine *the)
{
	PebbleHealth ph = xsmcGetHostDataValidate(xsThis, (void *)&xsHealthHooks);
	int32_t bpm;
	if (ph->onSample) {
		if (!ph->haveSample)
			return;
		bpm = ph->bpm;
		ph->haveSample = 0;
	} else {
		bpm = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);
	}
	xsSlot tmp;
	xsmcSetNewObject(xsResult);
	xsmcSetInteger(tmp, bpm);
	// use runtime xsID() to avoid depending on mc.xs.h having xsID_heartRate
	xsmcSet(xsResult, xsID("heartRate"), tmp);
}

static void healthEvent(HealthEventType type, void *context)
{
	if (type != HealthEventHeartRateUpdate)
		return;
	PebbleHealth ph = s_health;
	if (!ph || !ph->onSample)
		return;
	ph->bpm = (int32_t)health_service_peek_current_value(HealthMetricHeartRateBPM);
	ph->haveSample = 1;
	xsMachine *the = ph->the;
	xsBeginHost(the);
	xsCallFunction0(xsReference(ph->onSample), ph->obj);
	xsEndHost(the);
}
