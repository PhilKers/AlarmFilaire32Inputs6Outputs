/**
 * @file voice.c
 */
#include "voice.h"
#include <stdio.h>

const char *const voice_texts[VM_COUNT] = {
    [VM_NONE]        = "",
    [VM_ARMED_AWAY]  = "Système en service total.",
    [VM_ARMED_STAY]  = "Système en service partiel.",
    [VM_DISARMED]    = "Système hors service.",
    [VM_EXIT_DELAY]  = "Temporisation de sortie. Veuillez quitter les lieux.",
    [VM_ENTRY_DELAY] = "Bonjour. Veuillez saisir votre code.",
    [VM_INTRUSION]   = "Alarme intrusion.",
    [VM_FIRE]        = "Alarme incendie. Évacuez les lieux.",
    [VM_TAMPER]      = "Alarme sabotage.",
    [VM_TECHNICAL]   = "Alarme technique.",
    [VM_ZONE]        = "Zone",
    [VM_OPEN]        = "ouverte.",
    [VM_MAINS_FAIL]  = "Coupure secteur.",
    [VM_MAINS_OK]    = "Retour secteur.",
    [VM_BAT_LOW]     = "Batterie faible.",
    [VM_ARM_REFUSED] = "Mise en service impossible. Des zones sont ouvertes.",
    [VM_BAD_CODE]    = "Code erroné.",
    [VM_LOCKOUT]     = "Clavier verrouillé pendant trois minutes.",
    [VM_FAULT]       = "Défaut système.",
    [VM_CHIME]       = "Ding dong.",
    [VM_CALL_INTRO]  = "Ici votre centrale d'alarme.",
    [VM_CALL_MENU]   = "Appuyez sur 1 pour acquitter, 2 pour écouter à nouveau, 9 pour mettre hors service.",
    [VM_ACK]         = "Alarme acquittée. Au revoir.",
    [VM_BOX_TAMPER]  = "Ouverture du coffret de la centrale.",
    [VM_PANIC_CALL]  = "Alarme agression.",
};

void voice_filename(uint16_t id, char *out, size_t len)
{
    if (id >= 0x2000u)      snprintf(out, len, "/VOIX/Z%03u.WAV", (unsigned)(id - 0x2000u));
    else if (id >= 0x1000u) snprintf(out, len, "/VOIX/N%03u.WAV", (unsigned)(id - 0x1000u));
    else                    snprintf(out, len, "/VOIX/M%03u.WAV", (unsigned)id);
}

static unsigned add(uint16_t *seq, unsigned n, unsigned max, uint16_t id)
{
    if (n < max) seq[n++] = id;
    return n;
}

static unsigned add_zone(const alarm_config_t *cfg, uint8_t z, uint16_t *seq, unsigned n,
                         unsigned max)
{
    if (z >= NB_ZONES) {
        if (z == SRC_BOX) n = add(seq, n, max, VM_BOX_TAMPER);
        return n;
    }
    n = add(seq, n, max, VM_ZONE);
    n = add(seq, n, max, VOICE_NUM(z + 1u));
    if (cfg->zones[z].voice_id) n = add(seq, n, max, VOICE_ZNAME(cfg->zones[z].voice_id));
    return n;
}

unsigned voice_for_event(const alarm_config_t *cfg, const alarm_event_t *ev, bool local,
                         uint16_t *seq, unsigned max)
{
    unsigned n = 0;
    switch (ev->type) {
    case EV_ARMED_AWAY:  n = add(seq, n, max, VM_ARMED_AWAY);  break;
    case EV_ARMED_STAY:  n = add(seq, n, max, VM_ARMED_STAY);  break;
    case EV_DISARMED:    n = add(seq, n, max, VM_DISARMED);    break;
    case EV_EXIT_DELAY:  n = add(seq, n, max, VM_EXIT_DELAY);  break;
    case EV_ENTRY_DELAY: n = add(seq, n, max, VM_ENTRY_DELAY); break;
    case EV_ALARM_INTRUSION:
        n = add(seq, n, max, VM_INTRUSION);
        n = add_zone(cfg, ev->source, seq, n, max);
        break;
    case EV_ALARM_FIRE:
        n = add(seq, n, max, VM_FIRE);
        n = add_zone(cfg, ev->source, seq, n, max);
        break;
    case EV_ALARM_TAMPER:
        n = add(seq, n, max, VM_TAMPER);
        n = add_zone(cfg, ev->source, seq, n, max);
        break;
    case EV_ALARM_TECHNICAL:
        n = add(seq, n, max, VM_TECHNICAL);
        n = add_zone(cfg, ev->source, seq, n, max);
        break;
    case EV_ALARM_PANIC:
        /* Discrétion : jamais d'annonce locale d'une alarme agression */
        if (!local) n = add(seq, n, max, VM_PANIC_CALL);
        break;
    case EV_CHIME:
        n = add(seq, n, max, VM_CHIME);
        n = add_zone(cfg, ev->source, seq, n, max);
        n = add(seq, n, max, VM_OPEN);
        break;
    case EV_ARM_REFUSED: n = add(seq, n, max, VM_ARM_REFUSED); break;
    case EV_BAD_CODE:    n = add(seq, n, max, VM_BAD_CODE);    break;
    case EV_LOCKOUT:     n = add(seq, n, max, VM_LOCKOUT);     break;
    case EV_FAULT_ON:
        if (ev->source == FAULT_MAINS)            n = add(seq, n, max, VM_MAINS_FAIL);
        else if (ev->source == FAULT_BATTERY_LOW) n = add(seq, n, max, VM_BAT_LOW);
        else if (ev->source == FAULT_BOX_TAMPER)  n = add(seq, n, max, VM_BOX_TAMPER);
        else if (ev->source != FAULT_ETH && ev->source != FAULT_RTC)
            n = add(seq, n, max, VM_FAULT);
        break;
    case EV_FAULT_OFF:
        if (ev->source == FAULT_MAINS) n = add(seq, n, max, VM_MAINS_OK);
        break;
    default:
        break;
    }
    return n;
}
