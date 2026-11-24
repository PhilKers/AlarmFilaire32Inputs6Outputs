/**
 * @file outputs.h
 * @brief Logique des 6 sorties relais et de leur mode électrique.
 *
 * Chaque sortie utilise 3 relais pilotés par une chaîne de 3 TPIC6B595
 * (24 bits) :
 *   - K_MAIN  : relais de puissance (C / NO / NF vers le bornier)
 *   - K_SRC   : repos = commun relié au bornier COM (contact sec)
 *               travail = commun relié à la source interne
 *   - K_VSEL  : repos = +12 V (via PTC), travail = +5 V (via PTC)
 * Les 6 bits restants pilotent les LED de façade.
 *
 * Un changement de mode suit une séquence sûre « break-before-make » :
 * K_MAIN coupé → K_SRC coupé → K_VSEL positionné → K_SRC positionné,
 * avec 50 ms entre chaque étape. 12 V et 5 V ne sont jamais court-circuités.
 */
#ifndef OUTPUTS_H
#define OUTPUTS_H
#include "alarm_engine.h"

/* Affectation des bits dans le mot 24 bits (bit 0 = dernier étage de la chaîne) */
#define RLY_MAIN(i)     (1u << (i))
#define RLY_SRC(i)      (1u << (6u + (i)))
#define RLY_VSEL(i)     (1u << (12u + (i)))
#define LED_POWER       (1u << 18)
#define LED_ARMED       (1u << 19)
#define LED_ALARM       (1u << 20)
#define LED_FAULT       (1u << 21)
#define LED_NET         (1u << 22)
#define LED_GSM         (1u << 23)

#define OUT_SWITCH_STEP_MS  50u

typedef enum {
    SW_IDLE = 0,        /**< Mode stable                                    */
    SW_MAIN_OFF,        /**< Attente ouverture K_MAIN                       */
    SW_SRC_OFF,         /**< Attente ouverture K_SRC                        */
    SW_VSEL_SET,        /**< Attente basculement K_VSEL                     */
    SW_SRC_SET          /**< Attente fermeture K_SRC                        */
} sw_stage_t;

typedef struct {
    uint8_t    hw_mode;         /**< Mode réellement appliqué               */
    uint8_t    target_mode;
    uint8_t    stage;           /**< sw_stage_t                             */
    bool       remote;          /**< Commande manuelle                      */
    bool       last_logic;      /**< Pour détection de front (impulsion)    */
    bool       pulse_active;
    ms_t       pulse_end;
    ms_t       step_time;
    bool       main_on;         /**< État appliqué de K_MAIN                */
} out_channel_t;

typedef struct {
    out_channel_t ch[NB_OUTPUTS];
    uint32_t      word;         /**< Dernier mot calculé                    */
} outputs_t;

void     outputs_init(outputs_t *o, const alarm_config_t *cfg);
void     outputs_set_remote(outputs_t *o, unsigned idx, bool on);

/** Calcule le mot 24 bits à envoyer à la chaîne de registres. */
uint32_t outputs_update(outputs_t *o, const alarm_config_t *cfg,
                        const alarm_engine_t *e, ms_t now);

/** État logique (avant inversion/temporisation) d'une fonction de sortie. */
bool     output_function_state(const output_cfg_t *oc, const alarm_engine_t *e, ms_t now);
#endif
