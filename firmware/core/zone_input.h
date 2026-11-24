/**
 * @file zone_input.h
 * @brief Interprétation des mesures analogiques des 32 boucles.
 *
 * Chaque boucle est polarisée par une résistance de tirage de 4k7 vers VZONE
 * (3,3 V). L'ADC mesure la tension de boucle ; la plateforme fournit le
 * rapport V_boucle / VZONE en pour-mille (mesure ratiométrique, insensible
 * aux dérives de VZONE).
 */
#ifndef ZONE_INPUT_H
#define ZONE_INPUT_H
#include "alarm_config.h"

typedef struct {
    zone_state_t stable;        /**< État validé (après temps de réponse)  */
    zone_state_t candidate;     /**< État en cours de confirmation         */
    ms_t         since;         /**< Début de l'état candidat              */
    uint16_t     last_permille; /**< Dernière mesure (diagnostic)          */
} zone_filter_t;

typedef struct {
    zone_filter_t z[NB_ZONES];
} zone_inputs_t;

/** Callback appelé à chaque changement d'état validé. */
typedef void (*zone_change_cb_t)(void *ctx, unsigned zone, zone_state_t state, ms_t now);

loop_level_t  loop_classify(uint16_t permille, const loop_thresholds_t *t);
zone_state_t  zone_interpret(loop_level_t lvl, wiring_t wiring);

void zone_inputs_init(zone_inputs_t *zi);

/**
 * Traite une acquisition complète des 32 boucles.
 * @param permille  tableau de NB_ZONES mesures (0..1000+), ou NULL si la
 *                  référence est invalide (toutes zones en défaut).
 */
void zone_inputs_process(zone_inputs_t *zi, const alarm_config_t *cfg,
                         const uint16_t *permille, ms_t now,
                         zone_change_cb_t cb, void *ctx);
#endif
