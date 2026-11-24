/**
 * @file alarm_events.h
 * @brief Événements émis par le noyau (journal, notifications, voix, écran).
 */
#ifndef ALARM_EVENTS_H
#define ALARM_EVENTS_H
#include "alarm_types.h"

typedef enum {
    EV_NONE = 0,
    EV_BOOT,                /**< Démarrage centrale                        */
    EV_ARMED_AWAY,          /**< Mise en service totale effective          */
    EV_ARMED_STAY,          /**< Mise en service partielle effective       */
    EV_DISARMED,            /**< Mise hors service                         */
    EV_EXIT_DELAY,          /**< Début temporisation de sortie             */
    EV_ENTRY_DELAY,         /**< Début temporisation d'entrée (source=zone)*/
    EV_ALARM_INTRUSION,     /**< source = zone                             */
    EV_ALARM_FIRE,          /**< source = zone                             */
    EV_ALARM_PANIC,         /**< source = zone                             */
    EV_ALARM_TAMPER,        /**< source = zone ou SRC_BOX                  */
    EV_ALARM_TECHNICAL,     /**< source = zone                             */
    EV_ZONE_OPEN,
    EV_ZONE_CLOSE,
    EV_ZONE_TAMPER,
    EV_ZONE_FAULT,
    EV_CHIME,               /**< Carillon (zone ouverte, système désarmé)  */
    EV_FAULT_ON,            /**< source = fault_t                          */
    EV_FAULT_OFF,
    EV_SIREN_TIMEOUT,
    EV_ARM_REFUSED,         /**< Zones ouvertes / sabotage                 */
    EV_BAD_CODE,
    EV_LOCKOUT,
    EV_BYPASS_ON,           /**< Inhibition manuelle                       */
    EV_BYPASS_OFF,
    EV_AUTO_BYPASS,         /**< Inhibition automatique (zone bavarde)     */
    EV_OUTPUT_ON,           /**< Commande manuelle de sortie               */
    EV_OUTPUT_OFF,
    EV_CONFIG_CHANGED,
    EV_COUNT
} event_type_t;

#define SRC_NONE    0xFFu
#define SRC_BOX     0xFEu   /**< Autoprotection du coffret             */
#define USER_NONE   0xFFu

/** Origine d'une commande. */
typedef enum {
    ORIGIN_SYSTEM = 0,
    ORIGIN_SCREEN,
    ORIGIN_SMS,
    ORIGIN_PHONE_DTMF,
    ORIGIN_NETWORK
} origin_t;

typedef struct {
    uint32_t time;          /**< Horodatage UNIX (rempli par la plateforme) */
    uint16_t type;          /**< event_type_t                               */
    uint8_t  source;        /**< zone / défaut / sortie / SRC_xxx           */
    uint8_t  user;          /**< index utilisateur ou USER_NONE             */
    uint8_t  origin;        /**< origin_t                                   */
    uint8_t  reserved[3];
} alarm_event_t;

/** Libellé court en français (journal, écran, SMS). */
const char *event_label(event_type_t t);
const char *fault_label(fault_t f);
#endif
