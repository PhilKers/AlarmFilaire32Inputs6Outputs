/**
 * @file zone_input.c
 */
#include "zone_input.h"

/* Le sabotage est confirmé plus vite que l'intrusion (exigence usuelle). */
#define TAMPER_RESPONSE_MS  100u
#define FAULT_RESPONSE_MS   1000u

loop_level_t loop_classify(uint16_t permille, const loop_thresholds_t *t)
{
    if (permille > 1100u) return LOOP_INVALID;   /* Au-delà de VZONE : anormal */
    if (permille < t->short_max)  return LOOP_SHORT;
    if (permille < t->normal_max) return LOOP_NORMAL;
    if (permille < t->alarm_max)  return LOOP_ALARM;
    return LOOP_OPEN;
}

zone_state_t zone_interpret(loop_level_t lvl, wiring_t wiring)
{
    if (lvl == LOOP_INVALID) return ZSTATE_FAULT;
    switch (wiring) {
    case WIRING_NC:     /* Contact NF direct : fermé = court-circuit */
        return (lvl == LOOP_SHORT) ? ZSTATE_REST : ZSTATE_ACTIVE;
    case WIRING_NO:     /* Contact NO direct : fermé = détection */
        return (lvl == LOOP_SHORT) ? ZSTATE_ACTIVE : ZSTATE_REST;
    case WIRING_SEOL:   /* NF en série avec 4k7 */
        if (lvl == LOOP_NORMAL) return ZSTATE_REST;
        if (lvl == LOOP_SHORT)  return ZSTATE_TAMPER;
        return ZSTATE_ACTIVE;
    case WIRING_DEOL:   /* NF shunté par 4k7, en série avec 4k7 */
    default:
        if (lvl == LOOP_NORMAL) return ZSTATE_REST;
        if (lvl == LOOP_ALARM)  return ZSTATE_ACTIVE;
        return ZSTATE_TAMPER;   /* court-circuit ou coupure */
    }
}

void zone_inputs_init(zone_inputs_t *zi)
{
    for (unsigned i = 0; i < NB_ZONES; i++) {
        zi->z[i].stable = ZSTATE_REST;
        zi->z[i].candidate = ZSTATE_REST;
        zi->z[i].since = 0;
        zi->z[i].last_permille = 0;
    }
}

static ms_t response_time(const zone_cfg_t *zc, zone_state_t s)
{
    if (s == ZSTATE_TAMPER) return TAMPER_RESPONSE_MS;
    if (s == ZSTATE_FAULT)  return FAULT_RESPONSE_MS;
    return zc->response_ms;
}

void zone_inputs_process(zone_inputs_t *zi, const alarm_config_t *cfg,
                         const uint16_t *permille, ms_t now,
                         zone_change_cb_t cb, void *ctx)
{
    for (unsigned i = 0; i < NB_ZONES; i++) {
        const zone_cfg_t *zc = &cfg->zones[i];
        zone_filter_t *f = &zi->z[i];
        zone_state_t s;

        if (zc->type == ZT_DISABLED) {
            s = ZSTATE_REST;
        } else if (permille == NULL) {
            s = ZSTATE_FAULT;
        } else {
            f->last_permille = permille[i];
            s = zone_interpret(loop_classify(permille[i], &cfg->thresholds),
                               (wiring_t)zc->wiring);
        }

        if (s != f->candidate) {
            f->candidate = s;
            f->since = now;
        }
        if (f->candidate != f->stable &&
            time_reached(now, f->since + response_time(zc, f->candidate))) {
            f->stable = f->candidate;
            if (cb) cb(ctx, i, f->stable, now);
        }
    }
}
