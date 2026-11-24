/**
 * @file notifier.h
 * @brief File des notifications à distance (SMS et appels vocaux en cascade).
 *
 * Les SMS partent vers tous les contacts abonnés à l'événement. Les appels
 * sont passés dans l'ordre de la liste jusqu'à ce qu'un contact acquitte
 * (touche 1). Au plus NOTIF_CALL_ROUNDS tours de la liste.
 */
#ifndef NOTIFIER_H
#define NOTIFIER_H
#include "alarm_config.h"
#include "alarm_events.h"

#define NOTIF_QUEUE_LEN     24u
#define NOTIF_CALL_ROUNDS   3u
#define NOTIF_SMS_RETRIES   3u

typedef enum { JOB_SMS = 0, JOB_CALL } job_kind_t;

typedef struct {
    alarm_event_t ev;
    uint8_t       contact;
    uint8_t       kind;     /**< job_kind_t */
    uint8_t       tries;
    uint8_t       round;
} notif_job_t;

typedef struct {
    notif_job_t q[NOTIF_QUEUE_LEN];
    unsigned    count;
} notifier_t;

void     notifier_init(notifier_t *n);
uint16_t notif_class_of(const alarm_event_t *ev);
/** Génère les jobs pour un événement. Retourne le nombre de jobs ajoutés. */
unsigned notifier_on_event(notifier_t *n, const alarm_config_t *cfg, const alarm_event_t *ev);
/** Prochain job à traiter (priorité aux appels incendie/intrusion), NULL si vide. */
const notif_job_t *notifier_peek(const notifier_t *n);
/**
 * Compte-rendu du job en tête.
 * @param success  SMS envoyé / appel décroché ET acquitté
 */
void     notifier_done(notifier_t *n, const alarm_config_t *cfg, bool success);
/** Annule tous les appels en attente (mise hors service, acquittement). */
void     notifier_cancel_calls(notifier_t *n);
#endif
