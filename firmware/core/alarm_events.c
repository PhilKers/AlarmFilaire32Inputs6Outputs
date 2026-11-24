/**
 * @file alarm_events.c
 */
#include "alarm_events.h"

const char *event_label(event_type_t t)
{
    static const char *const lbl[EV_COUNT] = {
        [EV_NONE]            = "-",
        [EV_BOOT]            = "Demarrage centrale",
        [EV_ARMED_AWAY]      = "Mise en service totale",
        [EV_ARMED_STAY]      = "Mise en service partielle",
        [EV_DISARMED]        = "Mise hors service",
        [EV_EXIT_DELAY]      = "Temporisation de sortie",
        [EV_ENTRY_DELAY]     = "Temporisation d'entree",
        [EV_ALARM_INTRUSION] = "ALARME INTRUSION",
        [EV_ALARM_FIRE]      = "ALARME INCENDIE",
        [EV_ALARM_PANIC]     = "ALARME PANIQUE",
        [EV_ALARM_TAMPER]    = "ALARME SABOTAGE",
        [EV_ALARM_TECHNICAL] = "Alarme technique",
        [EV_ZONE_OPEN]       = "Zone ouverte",
        [EV_ZONE_CLOSE]      = "Zone fermee",
        [EV_ZONE_TAMPER]     = "Zone sabotage",
        [EV_ZONE_FAULT]      = "Zone en defaut",
        [EV_CHIME]           = "Carillon",
        [EV_FAULT_ON]        = "Defaut",
        [EV_FAULT_OFF]       = "Fin de defaut",
        [EV_SIREN_TIMEOUT]   = "Fin de sirene",
        [EV_ARM_REFUSED]     = "Mise en service refusee",
        [EV_BAD_CODE]        = "Code errone",
        [EV_LOCKOUT]         = "Clavier verrouille",
        [EV_BYPASS_ON]       = "Zone inhibee",
        [EV_BYPASS_OFF]      = "Fin d'inhibition",
        [EV_AUTO_BYPASS]     = "Zone inhibee (auto)",
        [EV_OUTPUT_ON]       = "Sortie activee",
        [EV_OUTPUT_OFF]      = "Sortie desactivee",
        [EV_CONFIG_CHANGED]  = "Configuration modifiee",
    };
    if ((unsigned)t >= EV_COUNT || lbl[t] == 0) return "?";
    return lbl[t];
}

const char *fault_label(fault_t f)
{
    static const char *const lbl[FAULT_COUNT] = {
        [FAULT_MAINS]           = "Coupure secteur",
        [FAULT_BATTERY_LOW]     = "Batterie basse",
        [FAULT_BATTERY_MISSING] = "Batterie absente",
        [FAULT_AUX_FUSE]        = "Fusible auxiliaire",
        [FAULT_OUT_FUSE]        = "Fusible sorties",
        [FAULT_ZONE_REF]        = "Reference boucles",
        [FAULT_GSM]             = "Reseau mobile",
        [FAULT_ETH]             = "Liaison Ethernet",
        [FAULT_RELAY_CHAIN]     = "Commande relais",
        [FAULT_BOX_TAMPER]      = "Autoprotection coffret",
        [FAULT_RTC]             = "Horloge",
    };
    if ((unsigned)f >= FAULT_COUNT) return "?";
    return lbl[f];
}
