/**
 * @file outputs.c
 */
#include "outputs.h"
#include <string.h>

void outputs_init(outputs_t *o, const alarm_config_t *cfg)
{
    memset(o, 0, sizeof *o);
    for (unsigned i = 0; i < NB_OUTPUTS; i++) {
        /* Au démarrage, tous les relais sont au repos : mode sec effectif,
         * puis la séquence amène chaque sortie dans son mode configuré. */
        o->ch[i].hw_mode = OUT_MODE_DRY;
        o->ch[i].target_mode = cfg->outputs[i].mode;
        o->ch[i].stage = (cfg->outputs[i].mode == OUT_MODE_DRY) ? SW_IDLE : SW_MAIN_OFF;
    }
}

void outputs_set_remote(outputs_t *o, unsigned idx, bool on)
{
    if (idx < NB_OUTPUTS) o->ch[idx].remote = on;
}

/* Signal clignotant : vrai pendant on_ms puis faux pendant off_ms. */
static bool blink(ms_t now, ms_t on_ms, ms_t off_ms)
{
    return (now % (on_ms + off_ms)) < on_ms;
}

bool output_function_state(const output_cfg_t *oc, const alarm_engine_t *e, ms_t now)
{
    switch ((out_function_t)oc->function) {
    case OUT_FN_SIREN:
        if (!e->siren_running) return false;
        /* Incendie : son modulé pour être distingué de l'intrusion */
        return (e->active_alarms & ALM_FIRE) ? blink(now, 1000, 1000) : true;
    case OUT_FN_SIREN_FIRE:
        return e->siren_running && (e->active_alarms & ALM_FIRE) && blink(now, 1000, 1000);
    case OUT_FN_STROBE:
        return (e->active_alarms & (ALM_INTRUSION | ALM_FIRE | ALM_TAMPER)) != 0;
    case OUT_FN_ARMED:
        /* Relais mécanique : pas de clignotement (usure), seule la LED clignote */
        return e->state != SYS_DISARMED;
    case OUT_FN_FAULT:
        return e->faults != 0;
    case OUT_FN_ZONE_FOLLOW:
        for (unsigned z = 0; z < NB_ZONES; z++)
            if ((oc->zone_mask & (1u << z)) && e->zone[z] != ZSTATE_REST) return true;
        return false;
    case OUT_FN_ENTRY_EXIT:
        return (e->state == SYS_EXIT_DELAY || e->state == SYS_ENTRY_DELAY) &&
               blink(now, 200, 800);
    case OUT_FN_PANIC:
        return (e->active_alarms & ALM_PANIC) != 0;
    case OUT_FN_TECHNICAL:
        return (e->active_alarms & ALM_TECHNICAL) != 0;
    case OUT_FN_REMOTE:     /* traité par l'appelant */
    case OUT_FN_NONE:
    default:
        return false;
    }
}

/* Fait avancer la séquence de changement de mode. Retourne vrai si stable. */
static bool mode_sequencer(out_channel_t *c, uint8_t cfg_mode, ms_t now)
{
    if (c->stage == SW_IDLE && cfg_mode != c->hw_mode) {
        c->target_mode = cfg_mode;
        c->stage = SW_MAIN_OFF;
        c->step_time = now;
    }
    if (c->stage == SW_IDLE) return true;

    if (!time_reached(now, c->step_time + OUT_SWITCH_STEP_MS)) return false;
    c->step_time = now;
    switch (c->stage) {
    case SW_MAIN_OFF: c->stage = SW_SRC_OFF;  break;
    case SW_SRC_OFF:  c->stage = SW_VSEL_SET; break;
    case SW_VSEL_SET: c->stage = SW_SRC_SET;  break;
    case SW_SRC_SET:
    default:
        c->hw_mode = c->target_mode;
        c->stage = SW_IDLE;
        /* Si la configuration a encore changé entre-temps, on recommence. */
        if (cfg_mode != c->hw_mode) {
            c->target_mode = cfg_mode;
            c->stage = SW_MAIN_OFF;
        }
        break;
    }
    return c->stage == SW_IDLE;
}

uint32_t outputs_update(outputs_t *o, const alarm_config_t *cfg,
                        const alarm_engine_t *e, ms_t now)
{
    uint32_t w = 0;
    for (unsigned i = 0; i < NB_OUTPUTS; i++) {
        const output_cfg_t *oc = &cfg->outputs[i];
        out_channel_t *c = &o->ch[i];

        bool logic = (oc->function == OUT_FN_REMOTE) ? c->remote
                                                     : output_function_state(oc, e, now);
        /* Mode impulsionnel : front montant → impulsion de pulse_s secondes */
        if (oc->pulse_s) {
            if (logic && !c->last_logic) {
                c->pulse_active = true;
                c->pulse_end = now + (ms_t)oc->pulse_s * 1000u;
            }
            if (c->pulse_active && time_reached(now, c->pulse_end)) c->pulse_active = false;
            c->last_logic = logic;
            logic = c->pulse_active;
        } else {
            c->last_logic = logic;
        }
        if (oc->invert) logic = !logic;

        bool stable = mode_sequencer(c, oc->mode, now);

        /* Relais de sélection de source */
        bool src_on, vsel_on;
        switch (c->stage) {
        case SW_MAIN_OFF:   /* source encore dans l'ancien mode */
            src_on  = c->hw_mode != OUT_MODE_DRY;
            vsel_on = c->hw_mode == OUT_MODE_5V;
            break;
        case SW_SRC_OFF:
            src_on  = false;
            vsel_on = c->hw_mode == OUT_MODE_5V;
            break;
        case SW_VSEL_SET:
            src_on  = false;
            vsel_on = c->target_mode == OUT_MODE_5V;
            break;
        case SW_SRC_SET:
            src_on  = c->target_mode != OUT_MODE_DRY;
            vsel_on = c->target_mode == OUT_MODE_5V;
            break;
        default:
            src_on  = c->hw_mode != OUT_MODE_DRY;
            vsel_on = c->hw_mode == OUT_MODE_5V;
            break;
        }
        c->main_on = stable && logic;
        if (c->main_on) w |= RLY_MAIN(i);
        if (src_on)     w |= RLY_SRC(i);
        if (vsel_on)    w |= RLY_VSEL(i);
    }

    /* LED de façade */
    bool on_mains = !(e->faults & (1u << FAULT_MAINS));
    if (on_mains || blink(now, 250, 1750)) w |= LED_POWER;
    if (e->state == SYS_ARMED || e->state == SYS_ENTRY_DELAY ||
        (e->state == SYS_EXIT_DELAY && blink(now, 500, 500))) w |= LED_ARMED;
    if (e->active_alarms || (e->alarm_memory && blink(now, 500, 500))) w |= LED_ALARM;
    if (e->faults & ~((1u << FAULT_ETH) | (1u << FAULT_GSM))) w |= LED_FAULT;
    if (!(e->faults & (1u << FAULT_ETH))) w |= LED_NET;
    if (!(e->faults & (1u << FAULT_GSM))) w |= LED_GSM;

    o->word = w;
    return w;
}
