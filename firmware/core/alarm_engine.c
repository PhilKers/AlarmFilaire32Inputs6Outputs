/**
 * @file alarm_engine.c
 */
#include "alarm_engine.h"
#include <string.h>

#define BIT(z) (1u << (z))

static void emit(alarm_engine_t *e, event_type_t t, uint8_t src, uint8_t user, origin_t o)
{
    if (!e->cb) return;
    alarm_event_t ev;
    memset(&ev, 0, sizeof ev);
    ev.type = (uint16_t)t;
    ev.source = src;
    ev.user = user;
    ev.origin = (uint8_t)o;
    e->cb(e->cb_ctx, &ev);
}

void engine_init(alarm_engine_t *e, const alarm_config_t *cfg,
                 engine_event_cb_t cb, void *ctx)
{
    memset(e, 0, sizeof *e);
    e->cfg = cfg;
    e->state = SYS_DISARMED;
    e->cb = cb;
    e->cb_ctx = ctx;
    for (unsigned i = 0; i < NB_ZONES; i++) e->zone[i] = ZSTATE_REST;
    emit(e, EV_BOOT, SRC_NONE, USER_NONE, ORIGIN_SYSTEM);
}

static inline zone_type_t ztype(const alarm_engine_t *e, unsigned z)
{
    return (zone_type_t)e->cfg->zones[z].type;
}

static inline bool is_intrusion_type(zone_type_t t)
{
    return t == ZT_INSTANT || t == ZT_DELAYED || t == ZT_FOLLOWER;
}

static inline bool bypassed(const alarm_engine_t *e, unsigned z)
{
    return ((e->bypass | e->auto_bypass) & BIT(z)) != 0;
}

bool engine_zone_included(const alarm_engine_t *e, unsigned z)
{
    if (z >= NB_ZONES || !is_intrusion_type(ztype(e, z)) || bypassed(e, z)) return false;
    if (e->mode == ARM_STAY) return (e->cfg->zones[z].flags & ZF_PARTIAL) != 0;
    return true;
}

static void start_siren(alarm_engine_t *e, ms_t now)
{
    e->siren_running = true;
    e->siren_until = now + (ms_t)e->cfg->siren_duration_s * 1000u;
}

/** Déclenchement intrusion avec protection « zone bavarde » (swinger). */
static void trigger_intrusion(alarm_engine_t *e, unsigned z, ms_t now)
{
    if (z < NB_ZONES) {
        uint8_t lim = e->cfg->swinger_limit;
        if (lim && e->alarm_count[z] >= lim) {
            if (!(e->auto_bypass & BIT(z))) {
                e->auto_bypass |= BIT(z);
                emit(e, EV_AUTO_BYPASS, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
            }
            return;
        }
        e->alarm_count[z]++;
        e->alarm_memory |= BIT(z);
    }
    if (e->state == SYS_ENTRY_DELAY) e->state = SYS_ARMED;
    e->active_alarms |= ALM_INTRUSION;
    emit(e, EV_ALARM_INTRUSION, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
    if (z >= NB_ZONES || !(e->cfg->zones[z].flags & ZF_SILENT)) start_siren(e, now);
}

static void trigger_tamper(alarm_engine_t *e, uint8_t src, ms_t now)
{
    e->active_alarms |= ALM_TAMPER;
    if (src < NB_ZONES) e->alarm_memory |= BIT(src);
    emit(e, EV_ALARM_TAMPER, src, USER_NONE, ORIGIN_SYSTEM);
    if (e->state != SYS_DISARMED || e->cfg->tamper_when_disarmed_siren) {
        if (e->state == SYS_ENTRY_DELAY) e->state = SYS_ARMED;
        start_siren(e, now);
    }
}

void engine_zone_update(alarm_engine_t *e, unsigned z, zone_state_t st, ms_t now)
{
    if (z >= NB_ZONES) return;
    zone_type_t t = ztype(e, z);
    e->zone[z] = st;
    if (t == ZT_DISABLED) return;

    switch (st) {
    case ZSTATE_REST:   emit(e, EV_ZONE_CLOSE, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM); return;
    case ZSTATE_FAULT:  emit(e, EV_ZONE_FAULT, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM); return;
    case ZSTATE_TAMPER:
        emit(e, EV_ZONE_TAMPER, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
        if (!bypassed(e, z)) trigger_tamper(e, (uint8_t)z, now);
        return;
    case ZSTATE_ACTIVE:
        emit(e, EV_ZONE_OPEN, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
        break;
    }

    if (bypassed(e, z)) return;
    const uint8_t flags = e->cfg->zones[z].flags;

    switch (t) {
    case ZT_FIRE:
        e->active_alarms |= ALM_FIRE;
        e->alarm_memory |= BIT(z);
        emit(e, EV_ALARM_FIRE, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
        start_siren(e, now);
        return;
    case ZT_PANIC:
        e->active_alarms |= ALM_PANIC;
        e->alarm_memory |= BIT(z);
        emit(e, EV_ALARM_PANIC, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
        if (!(flags & ZF_SILENT)) start_siren(e, now);
        return;
    case ZT_TECHNICAL:
        e->active_alarms |= ALM_TECHNICAL;
        emit(e, EV_ALARM_TECHNICAL, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
        return;
    case ZT_TAMPER:
        trigger_tamper(e, (uint8_t)z, now);
        return;
    case ZT_24H:
        trigger_intrusion(e, z, now);
        return;
    default:
        break;
    }

    /* Zones d'intrusion */
    if (e->state == SYS_DISARMED) {
        if (flags & ZF_CHIME) emit(e, EV_CHIME, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
        return;
    }
    if (!engine_zone_included(e, z)) return;

    switch (e->state) {
    case SYS_EXIT_DELAY:
        if (t == ZT_INSTANT) trigger_intrusion(e, z, now);
        break;
    case SYS_ARMED:
        if (t == ZT_DELAYED) {
            e->state = SYS_ENTRY_DELAY;
            e->entry_zone = (uint8_t)z;
            e->deadline = now + (ms_t)e->cfg->entry_delay_s * 1000u;
            emit(e, EV_ENTRY_DELAY, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
            if (e->cfg->entry_delay_s == 0) trigger_intrusion(e, z, now);
        } else {
            trigger_intrusion(e, z, now);
        }
        break;
    case SYS_ENTRY_DELAY:
        if (t == ZT_INSTANT) trigger_intrusion(e, z, now);
        break;
    default:
        break;
    }
}

arm_result_t engine_arm(alarm_engine_t *e, arm_mode_t mode, bool force,
                        uint8_t user, origin_t origin, ms_t now, uint32_t *open_mask)
{
    if (open_mask) *open_mask = 0;
    if (e->state != SYS_DISARMED) return ARM_ALREADY;

    arm_mode_t saved = e->mode;
    e->mode = mode;
    uint32_t blocking = 0, tamper = 0;
    for (unsigned z = 0; z < NB_ZONES; z++) {
        if (ztype(e, z) == ZT_DISABLED || bypassed(e, z)) continue;
        if (e->zone[z] == ZSTATE_TAMPER || e->zone[z] == ZSTATE_FAULT) tamper |= BIT(z);
        else if (e->zone[z] == ZSTATE_ACTIVE && engine_zone_included(e, z) &&
                 ztype(e, z) == ZT_INSTANT) blocking |= BIT(z);
    }

    if (tamper) {
        e->mode = saved;
        if (open_mask) *open_mask = tamper;
        emit(e, EV_ARM_REFUSED, SRC_NONE, user, origin);
        return ARM_TAMPER_PRESENT;
    }
    if (blocking) {
        uint32_t not_bypassable = 0;
        for (unsigned z = 0; z < NB_ZONES; z++)
            if ((blocking & BIT(z)) && !(e->cfg->zones[z].flags & ZF_BYPASSABLE))
                not_bypassable |= BIT(z);
        if (!force || not_bypassable) {
            e->mode = saved;
            if (open_mask) *open_mask = force ? not_bypassable : blocking;
            emit(e, EV_ARM_REFUSED, SRC_NONE, user, origin);
            return ARM_ZONES_OPEN;
        }
        for (unsigned z = 0; z < NB_ZONES; z++)
            if (blocking & BIT(z)) {
                e->bypass |= BIT(z);
                emit(e, EV_BYPASS_ON, (uint8_t)z, user, origin);
            }
    }

    memset(e->alarm_count, 0, sizeof e->alarm_count);
    e->alarm_memory = 0;
    e->active_alarms &= (uint8_t)~(ALM_INTRUSION | ALM_TAMPER);
    if (e->cfg->exit_delay_s == 0) {
        e->state = SYS_ARMED;
        emit(e, mode == ARM_AWAY ? EV_ARMED_AWAY : EV_ARMED_STAY, SRC_NONE, user, origin);
    } else {
        e->state = SYS_EXIT_DELAY;
        e->deadline = now + (ms_t)e->cfg->exit_delay_s * 1000u;
        emit(e, EV_EXIT_DELAY, (uint8_t)mode, user, origin);
    }
    return ARM_OK;
}

void engine_disarm(alarm_engine_t *e, uint8_t user, origin_t origin, ms_t now)
{
    (void)now;
    e->state = SYS_DISARMED;
    e->siren_running = false;
    e->active_alarms = 0;
    e->bypass = 0;
    e->auto_bypass = 0;
    /* alarm_memory conservée pour affichage jusqu'à la prochaine MES */
    emit(e, EV_DISARMED, SRC_NONE, user, origin);
}

void engine_panic(alarm_engine_t *e, uint8_t user, origin_t origin, ms_t now)
{
    e->active_alarms |= ALM_PANIC;
    emit(e, EV_ALARM_PANIC, SRC_NONE, user, origin);
    start_siren(e, now);
}

bool engine_set_bypass(alarm_engine_t *e, unsigned z, bool on, uint8_t user)
{
    if (z >= NB_ZONES || e->state != SYS_DISARMED) return false;
    if (on && !(e->cfg->zones[z].flags & ZF_BYPASSABLE)) return false;
    bool cur = (e->bypass & BIT(z)) != 0;
    if (cur == on) return true;
    if (on) e->bypass |= BIT(z); else e->bypass &= ~BIT(z);
    emit(e, on ? EV_BYPASS_ON : EV_BYPASS_OFF, (uint8_t)z, user, ORIGIN_SCREEN);
    return true;
}

void engine_set_fault(alarm_engine_t *e, fault_t f, bool active, ms_t now)
{
    (void)now;
    if ((unsigned)f >= FAULT_COUNT) return;
    bool cur = (e->faults & BIT(f)) != 0;
    if (cur == active) return;
    if (active) e->faults |= BIT(f); else e->faults &= ~BIT(f);
    emit(e, active ? EV_FAULT_ON : EV_FAULT_OFF, (uint8_t)f, USER_NONE, ORIGIN_SYSTEM);
}

void engine_set_box_tamper(alarm_engine_t *e, bool open, ms_t now)
{
    if (open == e->box_tamper) return;
    e->box_tamper = open;
    engine_set_fault(e, FAULT_BOX_TAMPER, open, now);
    if (open) trigger_tamper(e, SRC_BOX, now);
}

void engine_tick(alarm_engine_t *e, ms_t now)
{
    if (e->state == SYS_EXIT_DELAY && time_reached(now, e->deadline)) {
        e->state = SYS_ARMED;
        emit(e, e->mode == ARM_AWAY ? EV_ARMED_AWAY : EV_ARMED_STAY, SRC_NONE, USER_NONE,
             ORIGIN_SYSTEM);
        /* Zones encore ouvertes en fin de tempo de sortie */
        for (unsigned z = 0; z < NB_ZONES && e->state == SYS_ARMED; z++) {
            if (e->zone[z] != ZSTATE_ACTIVE || !engine_zone_included(e, z)) continue;
            zone_type_t t = ztype(e, z);
            if (t == ZT_DELAYED || t == ZT_FOLLOWER) {
                e->state = SYS_ENTRY_DELAY;
                e->entry_zone = (uint8_t)z;
                e->deadline = now + (ms_t)e->cfg->entry_delay_s * 1000u;
                emit(e, EV_ENTRY_DELAY, (uint8_t)z, USER_NONE, ORIGIN_SYSTEM);
            } else {
                trigger_intrusion(e, z, now);
            }
        }
    }
    if (e->state == SYS_ENTRY_DELAY && time_reached(now, e->deadline)) {
        trigger_intrusion(e, e->entry_zone, now);
        e->state = SYS_ARMED;
    }
    if (e->siren_running && time_reached(now, e->siren_until)) {
        e->siren_running = false;
        emit(e, EV_SIREN_TIMEOUT, SRC_NONE, USER_NONE, ORIGIN_SYSTEM);
    }
}

uint32_t engine_delay_remaining_s(const alarm_engine_t *e, ms_t now)
{
    if (e->state != SYS_EXIT_DELAY && e->state != SYS_ENTRY_DELAY) return 0;
    if (time_reached(now, e->deadline)) return 0;
    return (e->deadline - now + 999u) / 1000u;
}

const char *engine_state_label(const alarm_engine_t *e)
{
    if (e->active_alarms & ALM_FIRE)      return "ALARME INCENDIE";
    if (e->active_alarms & (ALM_INTRUSION | ALM_TAMPER | ALM_PANIC))
        return "ALARME";
    switch (e->state) {
    case SYS_DISARMED:    return "Desarme";
    case SYS_EXIT_DELAY:  return "Sortie en cours";
    case SYS_ARMED:       return e->mode == ARM_AWAY ? "Arme (total)" : "Arme (partiel)";
    case SYS_ENTRY_DELAY: return "Entree : saisir le code";
    }
    return "?";
}
