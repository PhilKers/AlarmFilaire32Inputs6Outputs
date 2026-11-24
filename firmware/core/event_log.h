/**
 * @file event_log.h
 * @brief Journal circulaire des événements (RAM, recopié sur carte SD).
 */
#ifndef EVENT_LOG_H
#define EVENT_LOG_H
#include "alarm_events.h"

#define EVENT_LOG_SIZE  512u    /* puissance de 2 */

typedef struct {
    alarm_event_t buf[EVENT_LOG_SIZE];
    uint32_t      head;     /**< Nombre total d'événements écrits */
} event_log_t;

void     event_log_init(event_log_t *l);
void     event_log_push(event_log_t *l, const alarm_event_t *ev);
uint32_t event_log_count(const event_log_t *l);
/** idx 0 = plus récent. Retourne false si hors limites. */
bool     event_log_get(const event_log_t *l, uint32_t idx, alarm_event_t *out);

/** Formate une ligne lisible : "2026-11-24 14:05:12 ALARME INTRUSION - Zone 3 Garage". */
int      event_format(const alarm_event_t *ev, const void *cfg, char *out, size_t len);
#endif
