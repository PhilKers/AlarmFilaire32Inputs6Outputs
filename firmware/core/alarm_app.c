/**
 * @file alarm_app.c
 */
#include "alarm_app.h"
#include <string.h>

static void on_event(void *ctx, const alarm_event_t *evin)
{
    alarm_app_t *a = (alarm_app_t *)ctx;
    alarm_event_t ev = *evin;
    ev.time = a->hooks.unix_time ? a->hooks.unix_time() : 0;

    event_log_push(&a->log, &ev);
    if (a->hooks.event_persist) a->hooks.event_persist(&ev);

    notifier_on_event(&a->notifier, a->cfg, &ev);
    if (ev.type == EV_DISARMED) notifier_cancel_calls(&a->notifier);

    if (a->cfg->voice_enabled && a->hooks.voice_play) {
        uint16_t seq[VOICE_MAX_SEQ];
        unsigned n = voice_for_event(a->cfg, &ev, true, seq, VOICE_MAX_SEQ);
        if (n) a->hooks.voice_play(seq, n);
    }
    if (a->hooks.ui_notify) a->hooks.ui_notify(&ev);
}

static void on_zone_change(void *ctx, unsigned zone, zone_state_t st, ms_t now)
{
    alarm_app_t *a = (alarm_app_t *)ctx;
    engine_zone_update(&a->engine, zone, st, now);
}

void alarm_app_init(alarm_app_t *a, alarm_config_t *cfg, const app_hooks_t *hooks)
{
    memset(a, 0, sizeof *a);
    a->cfg = cfg;
    if (hooks) a->hooks = *hooks;
    zone_inputs_init(&a->inputs);
    event_log_init(&a->log);
    notifier_init(&a->notifier);
    power_monitor_init(&a->power);
    outputs_init(&a->outputs, cfg);
    engine_init(&a->engine, cfg, on_event, a);
}

void alarm_app_zones(alarm_app_t *a, const uint16_t *permille, ms_t now)
{
    zone_inputs_process(&a->inputs, a->cfg, permille, now, on_zone_change, a);
}

void alarm_app_power(alarm_app_t *a, const power_meas_t *m, ms_t now)
{
    a->last_power = *m;
    power_monitor_update(&a->power, a->cfg, &a->engine, m, now);
}

uint32_t alarm_app_tick(alarm_app_t *a, ms_t now)
{
    engine_tick(&a->engine, now);
    return outputs_update(&a->outputs, a->cfg, &a->engine, now);
}

static void bad_code_event(alarm_app_t *a, auth_result_t r, origin_t o)
{
    alarm_event_t ev = { 0 };
    ev.type = (r == AUTH_LOCKED || a->guard_screen.locked) ? EV_LOCKOUT : EV_BAD_CODE;
    ev.source = SRC_NONE; ev.user = USER_NONE; ev.origin = (uint8_t)o;
    on_event(a, &ev);
}

auth_result_t alarm_app_screen_auth(alarm_app_t *a, const char *pin, ms_t now, int *user)
{
    auth_result_t r = users_authenticate(a->cfg, &a->guard_screen, pin, now, user);
    if (r != AUTH_OK) bad_code_event(a, r, ORIGIN_SCREEN);
    return r;
}

arm_result_t alarm_app_arm(alarm_app_t *a, const char *pin, arm_mode_t mode, bool force,
                           ms_t now, uint32_t *open_mask)
{
    int user;
    if (alarm_app_screen_auth(a, pin, now, &user) != AUTH_OK) {
        if (open_mask) *open_mask = 0;
        return ARM_BAD_CODE;
    }
    return engine_arm(&a->engine, mode, force, (uint8_t)user, ORIGIN_SCREEN, now, open_mask);
}

bool alarm_app_disarm(alarm_app_t *a, const char *pin, origin_t origin, ms_t now)
{
    int user;
    if (alarm_app_screen_auth(a, pin, now, &user) != AUTH_OK) return false;
    engine_disarm(&a->engine, (uint8_t)user, origin, now);
    return true;
}

bool alarm_app_remote(alarm_app_t *a, const char *text, origin_t origin, ms_t now,
                      char *reply, size_t len)
{
    remote_ctx_t rc = { a->cfg, &a->engine, &a->outputs, &a->guard_remote };
    return remote_cmd_execute(&rc, text, origin, now, reply, len);
}
