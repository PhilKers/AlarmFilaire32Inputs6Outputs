/**
 * @file power_monitor.c
 */
#include "power_monitor.h"
#include <string.h>

#define BAT_LOW_CONFIRM_MS  10000u
#define BAT_MISSING_MV      6000u
#define AUX_MIN_MV          9000u
#define VZONE_MIN_MV        3150u
#define VZONE_MAX_MV        3450u

void power_monitor_init(power_monitor_t *p) { memset(p, 0, sizeof *p); }

void power_monitor_update(power_monitor_t *p, const alarm_config_t *cfg,
                          alarm_engine_t *e, const power_meas_t *m, ms_t now)
{
    /* Secteur : défaut confirmé après le délai configuré (évite les micro-coupures) */
    if (!m->ac_ok) {
        if (!p->mains_lost) { p->mains_lost = true; p->mains_lost_since = now; }
        if (time_reached(now, p->mains_lost_since + (ms_t)cfg->mains_fail_delay_s * 1000u))
            engine_set_fault(e, FAULT_MAINS, true, now);
    } else {
        p->mains_lost = false;
        engine_set_fault(e, FAULT_MAINS, false, now);
    }

    /* Batterie absente */
    engine_set_fault(e, FAULT_BATTERY_MISSING, m->vbat_mv < BAT_MISSING_MV, now);

    /* Batterie basse avec hystérésis */
    bool bat_low_now = (e->faults >> FAULT_BATTERY_LOW) & 1u;
    if (m->vbat_mv >= BAT_MISSING_MV && m->vbat_mv < cfg->bat_low_mv) {
        if (p->bat_low_since == 0) p->bat_low_since = now ? now : 1;
        if (time_reached(now, p->bat_low_since + BAT_LOW_CONFIRM_MS))
            engine_set_fault(e, FAULT_BATTERY_LOW, true, now);
    } else {
        p->bat_low_since = 0;
        if (bat_low_now && m->vbat_mv >= cfg->bat_ok_mv)
            engine_set_fault(e, FAULT_BATTERY_LOW, false, now);
    }

    /* Fusibles réarmables (PTC) : tension aval effondrée alors que l'entrée est présente */
    bool vin_ok = m->vin_mv > 10000u;
    engine_set_fault(e, FAULT_AUX_FUSE, vin_ok && m->vaux_mv < AUX_MIN_MV, now);
    engine_set_fault(e, FAULT_OUT_FUSE, vin_ok && m->vout12_mv < AUX_MIN_MV, now);

    /* Référence des boucles */
    engine_set_fault(e, FAULT_ZONE_REF, m->vzone_mv < VZONE_MIN_MV || m->vzone_mv > VZONE_MAX_MV,
                     now);
}
