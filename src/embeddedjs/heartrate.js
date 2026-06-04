// Native module stub — implementation is in heartrate.c
// C-side health_relay.c writes to g_heart_rate; get() reads it directly.
export default class HeartRate @ "xs_heartrate_destructor" {
    static get() @ "xs_heartrate_get";
}
