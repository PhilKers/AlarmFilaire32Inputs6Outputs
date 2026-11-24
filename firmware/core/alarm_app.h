/**
 * @file alarm_app.h
 * @brief Assemblage du noyau : relie entrées, moteur, sorties, journal,
 *        notifications et voix. La plateforme (STM32 ou simulateur PC)
 *        fournit uniquement les fonctions de app_hooks_t.
 */
#ifndef ALARM_APP_H
#define ALARM_APP_H
#include "alarm_engine.h"
#include "zone_input.h"
#include "outputs.h"
#include "power_monitor.h"
#include "event_log.h"
#include "notifier.h"
#include "voice.h"
#include "users.h"
#include "remote_cmd.h"

typedef struct {
    uint32_t (*unix_time)(void);
    /** Lecture vocale locale d'une séquence de fragments (non bloquant). */
    void     (*voice_play)(const uint16_t *seq, unsigned n);
    /** Persistance d'un événement (SD). Peut être NULL. */
    void     (*event_persist)(const alarm_event_t *ev);
    /** Notification d'un événement à l'IHM (rafraîchissement). Peut être NULL. */
    void     (*ui_notify)(const alarm_event_t *ev);
} app_hooks_t;

typedef struct {
    alarm_config_t   *cfg;
    app_hooks_t       hooks;
    zone_inputs_t     inputs;
    alarm_engine_t    engine;
    outputs_t         outputs;
    power_monitor_t   power;
    event_log_t       log;
    notifier_t        notifier;
    auth_guard_t      guard_screen;
    auth_guard_t      guard_remote;
    power_meas_t      last_power;
} alarm_app_t;

void     alarm_app_init(alarm_app_t *a, alarm_config_t *cfg, const app_hooks_t *hooks);

/** Cycle d'acquisition des boucles (toutes les 10 ms). NULL = référence HS. */
void     alarm_app_zones(alarm_app_t *a, const uint16_t *permille, ms_t now);
void     alarm_app_power(alarm_app_t *a, const power_meas_t *m, ms_t now);
/** Tick moteur + calcul du mot relais. Retourne le mot 24 bits. */
uint32_t alarm_app_tick(alarm_app_t *a, ms_t now);

/* Commandes utilisateur depuis l'écran (code en clair, vérifié ici) */
auth_result_t alarm_app_screen_auth(alarm_app_t *a, const char *pin, ms_t now, int *user);
arm_result_t  alarm_app_arm(alarm_app_t *a, const char *pin, arm_mode_t mode, bool force,
                            ms_t now, uint32_t *open_mask);
bool          alarm_app_disarm(alarm_app_t *a, const char *pin, origin_t origin, ms_t now);

/** Point d'entrée des SMS reçus et des commandes réseau. */
bool     alarm_app_remote(alarm_app_t *a, const char *text, origin_t origin, ms_t now,
                          char *reply, size_t len);
#endif
