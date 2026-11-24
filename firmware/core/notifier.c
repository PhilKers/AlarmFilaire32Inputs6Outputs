/**
 * @file notifier.c
 */
#include "notifier.h"
#include <string.h>

void notifier_init(notifier_t *n) { memset(n, 0, sizeof *n); }

uint16_t notif_class_of(const alarm_event_t *ev)
{
    switch (ev->type) {
    case EV_ALARM_INTRUSION: return NOTIF_INTRUSION;
    case EV_ALARM_FIRE:      return NOTIF_FIRE;
    case EV_ALARM_PANIC:     return NOTIF_PANIC;
    case EV_ALARM_TAMPER:    return NOTIF_TAMPER;
    case EV_ALARM_TECHNICAL: return NOTIF_TECHNICAL;
    case EV_ARMED_AWAY: case EV_ARMED_STAY: case EV_DISARMED:
        return NOTIF_ARMING;
    case EV_FAULT_ON: case EV_FAULT_OFF:
        /* Les défauts de communication ne sont pas notifiés par… la communication */
        if (ev->source == FAULT_GSM || ev->source == FAULT_ETH || ev->source == FAULT_RTC)
            return 0;
        return NOTIF_FAULT;
    default:
        return 0;
    }
}

static bool push(notifier_t *n, const notif_job_t *j)
{
    if (n->count >= NOTIF_QUEUE_LEN) return false;
    n->q[n->count++] = *j;
    return true;
}

static void pop(notifier_t *n)
{
    if (!n->count) return;
    memmove(&n->q[0], &n->q[1], (n->count - 1u) * sizeof n->q[0]);
    n->count--;
}

unsigned notifier_on_event(notifier_t *n, const alarm_config_t *cfg, const alarm_event_t *ev)
{
    uint16_t cls = notif_class_of(ev);
    if (!cls) return 0;
    unsigned added = 0;
    bool is_alarm = (cls & (NOTIF_INTRUSION | NOTIF_FIRE | NOTIF_PANIC | NOTIF_TAMPER |
                            NOTIF_TECHNICAL)) != 0;
    bool call_queued = false;

    for (unsigned i = 0; i < NB_CONTACTS; i++) {
        const contact_cfg_t *c = &cfg->contacts[i];
        if (!c->phone[0] || !(c->event_mask & cls)) continue;
        notif_job_t j;
        memset(&j, 0, sizeof j);
        j.ev = *ev;
        j.contact = (uint8_t)i;
        if (c->sms) { j.kind = JOB_SMS; added += push(n, &j); }
        /* Un seul job d'appel : il parcourt la liste en cascade */
        if (is_alarm && c->call && !call_queued) {
            j.kind = JOB_CALL;
            if (push(n, &j)) { added++; call_queued = true; }
        }
    }
    return added;
}

const notif_job_t *notifier_peek(const notifier_t *n)
{
    return n->count ? &n->q[0] : NULL;
}

static int next_call_contact(const alarm_config_t *cfg, uint16_t cls, unsigned after)
{
    for (unsigned i = after + 1u; i < NB_CONTACTS; i++) {
        const contact_cfg_t *c = &cfg->contacts[i];
        if (c->phone[0] && c->call && (c->event_mask & cls)) return (int)i;
    }
    return -1;
}

void notifier_done(notifier_t *n, const alarm_config_t *cfg, bool success)
{
    if (!n->count) return;
    notif_job_t j = n->q[0];
    pop(n);
    if (success) return;     /* SMS envoyé, ou appel acquitté : cascade terminée */

    if (j.kind == JOB_SMS) {
        if (++j.tries < NOTIF_SMS_RETRIES) push(n, &j);
        return;
    }
    /* Appel non acquitté : contact suivant, puis nouveau tour */
    uint16_t cls = notif_class_of(&j.ev);
    int nx = next_call_contact(cfg, cls, j.contact);
    if (nx < 0) {
        if (++j.round >= NOTIF_CALL_ROUNDS) return;
        nx = next_call_contact(cfg, cls, (unsigned)-1);
        if (nx < 0) return;
    }
    j.contact = (uint8_t)nx;
    push(n, &j);
}

void notifier_cancel_calls(notifier_t *n)
{
    unsigned w = 0;
    for (unsigned r = 0; r < n->count; r++)
        if (n->q[r].kind != JOB_CALL) n->q[w++] = n->q[r];
    n->count = w;
}
