/**
 * @file alarm_engine.h
 * @brief Machine d'états de la centrale : armement, temporisations, alarmes.
 *
 *  DISARMED ──arm()──► EXIT_DELAY ──(tempo écoulée)──► ARMED
 *     ▲                     │                          │ zone temporisée
 *     │                     │ zone immédiate           ▼
 *     └──── disarm() ◄──────┴──────── ALARME ◄──── ENTRY_DELAY
 *
 *  Les alarmes 24h/24 (incendie, panique, sabotage, technique) sont
 *  indépendantes de l'état d'armement.
 */
#ifndef ALARM_ENGINE_H
#define ALARM_ENGINE_H
#include "alarm_config.h"
#include "alarm_events.h"

typedef enum {
    SYS_DISARMED = 0,
    SYS_EXIT_DELAY,
    SYS_ARMED,
    SYS_ENTRY_DELAY
} sys_state_t;

typedef enum {
    ARM_AWAY = 0,           /**< Totale   */
    ARM_STAY                /**< Partielle */
} arm_mode_t;

/* Alarmes actives (mémorisées jusqu'à la mise hors service / acquittement) */
#define ALM_INTRUSION   (1u << 0)
#define ALM_FIRE        (1u << 1)
#define ALM_PANIC       (1u << 2)
#define ALM_TAMPER      (1u << 3)
#define ALM_TECHNICAL   (1u << 4)

typedef enum {
    ARM_OK = 0,
    ARM_ALREADY,
    ARM_ZONES_OPEN,         /**< Zones immédiates ouvertes (voir mask)     */
    ARM_TAMPER_PRESENT,     /**< Sabotage en cours                         */
    ARM_BAD_CODE            /**< Code refusé ou clavier verrouillé         */
} arm_result_t;

typedef void (*engine_event_cb_t)(void *ctx, const alarm_event_t *ev);

typedef struct {
    const alarm_config_t *cfg;
    sys_state_t   state;
    arm_mode_t    mode;
    ms_t          deadline;         /**< Fin de tempo d'entrée/sortie     */
    uint8_t       entry_zone;
    zone_state_t  zone[NB_ZONES];
    uint32_t      bypass;           /**< Inhibitions manuelles            */
    uint32_t      auto_bypass;      /**< Inhibitions automatiques         */
    uint32_t      alarm_memory;     /**< Zones ayant déclenché            */
    uint8_t       alarm_count[NB_ZONES];
    uint8_t       active_alarms;    /**< ALM_xxx                          */
    bool          siren_running;
    ms_t          siren_until;
    uint32_t      faults;           /**< bit = fault_t                    */
    bool          box_tamper;
    engine_event_cb_t cb;
    void         *cb_ctx;
} alarm_engine_t;

void engine_init(alarm_engine_t *e, const alarm_config_t *cfg,
                 engine_event_cb_t cb, void *ctx);

/** Changement d'état validé d'une zone (appelé par zone_inputs). */
void engine_zone_update(alarm_engine_t *e, unsigned zone, zone_state_t st, ms_t now);

/**
 * Demande de mise en service.
 * @param force      inhibe automatiquement les zones ouvertes inhibables
 * @param open_mask  [out] zones empêchant la MES (peut être NULL)
 */
arm_result_t engine_arm(alarm_engine_t *e, arm_mode_t mode, bool force,
                        uint8_t user, origin_t origin, ms_t now, uint32_t *open_mask);

/** Mise hors service / acquittement des alarmes (code déjà vérifié). */
void engine_disarm(alarm_engine_t *e, uint8_t user, origin_t origin, ms_t now);

/** Déclenche une alarme panique (bouton écran, DTMF…). */
void engine_panic(alarm_engine_t *e, uint8_t user, origin_t origin, ms_t now);

/** Inhibition manuelle (uniquement désarmé, zone ZF_BYPASSABLE). */
bool engine_set_bypass(alarm_engine_t *e, unsigned zone, bool on, uint8_t user);

void engine_set_fault(alarm_engine_t *e, fault_t f, bool active, ms_t now);
void engine_set_box_tamper(alarm_engine_t *e, bool open, ms_t now);

/** À appeler périodiquement (≤ 100 ms). */
void engine_tick(alarm_engine_t *e, ms_t now);

/** Zone prise en compte pour l'intrusion dans le mode courant ? */
bool engine_zone_included(const alarm_engine_t *e, unsigned zone);

/** Secondes restantes de temporisation (0 si aucune). */
uint32_t engine_delay_remaining_s(const alarm_engine_t *e, ms_t now);

const char *engine_state_label(const alarm_engine_t *e);
#endif
