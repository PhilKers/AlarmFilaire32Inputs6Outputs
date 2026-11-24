/**
 * @file alarm_types.h
 * @brief Types et constantes communs du noyau de la centrale d'alarme.
 *
 * Le noyau (dossier core/) est 100 % portable : il ne dépend ni de la HAL
 * STM32, ni de FreeRTOS. Il est compilé et testé sur PC (dossier tests/) et
 * intégré tel quel dans le firmware STM32H743.
 */
#ifndef ALARM_TYPES_H
#define ALARM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Dimensionnement                                                          */
/* ------------------------------------------------------------------------ */
#define NB_ZONES            32u
#define NB_OUTPUTS          6u
#define NB_USERS            16u
#define NB_CONTACTS         8u
#define NAME_LEN            24u
#define PHONE_LEN           20u

/** Temps en millisecondes (horloge monotone, débordement ~49 jours géré). */
typedef uint32_t ms_t;

/** Comparaison robuste au débordement : vrai si a est atteint ou dépassé par now. */
static inline bool time_reached(ms_t now, ms_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

/* ------------------------------------------------------------------------ */
/* Zones (entrées filaires)                                                 */
/* ------------------------------------------------------------------------ */

/** Niveau électrique mesuré sur la boucle (classification ADC). */
typedef enum {
    LOOP_SHORT = 0,     /**< Court-circuit (≈ 0 Ω)                    */
    LOOP_NORMAL,        /**< 1 résistance de fin de ligne (4k7)        */
    LOOP_ALARM,         /**< 2 résistances en série (9k4)              */
    LOOP_OPEN,          /**< Circuit ouvert / coupé                    */
    LOOP_INVALID        /**< Mesure impossible (référence absente)     */
} loop_level_t;

/** Câblage de la boucle, configurable par zone. */
typedef enum {
    WIRING_NC = 0,      /**< Contact NF direct, sans résistance        */
    WIRING_NO,          /**< Contact NO direct, sans résistance        */
    WIRING_SEOL,        /**< NF + 1 résistance fin de ligne 4k7        */
    WIRING_DEOL         /**< NF + double résistance 4k7 / 4k7 (sabotage)*/
} wiring_t;

/** État logique de la zone après interprétation du câblage. */
typedef enum {
    ZSTATE_REST = 0,    /**< Repos (contact fermé)                     */
    ZSTATE_ACTIVE,      /**< Ouverte / en détection                    */
    ZSTATE_TAMPER,      /**< Sabotage (coupure ou court-circuit)       */
    ZSTATE_FAULT        /**< Défaut de mesure                          */
} zone_state_t;

/** Type fonctionnel de la zone. */
typedef enum {
    ZT_DISABLED = 0,    /**< Zone non utilisée                         */
    ZT_INSTANT,         /**< Intrusion immédiate                       */
    ZT_DELAYED,         /**< Temporisée (porte d'entrée)               */
    ZT_FOLLOWER,        /**< Suiveuse : immédiate sauf pendant tempo   */
    ZT_24H,             /**< Intrusion 24h/24                          */
    ZT_FIRE,            /**< Incendie 24h/24                           */
    ZT_PANIC,           /**< Panique / agression 24h/24                */
    ZT_TECHNICAL,       /**< Technique (inondation, gel…) sans sirène  */
    ZT_TAMPER           /**< Autoprotection 24h/24                     */
} zone_type_t;

/* Drapeaux de zone */
#define ZF_PARTIAL      (1u << 0)   /**< Incluse dans la mise en service partielle */
#define ZF_CHIME        (1u << 1)   /**< Carillon vocal à l'ouverture (désarmé)    */
#define ZF_BYPASSABLE   (1u << 2)   /**< Inhibition autorisée                       */
#define ZF_SILENT       (1u << 3)   /**< Alarme silencieuse (pas de sirène)         */

/* ------------------------------------------------------------------------ */
/* Sorties relais                                                           */
/* ------------------------------------------------------------------------ */

/** Mode électrique d'une sortie (sélectionné par relais, piloté par logiciel). */
typedef enum {
    OUT_MODE_DRY = 0,   /**< Contact sec (boucle) : C / NO / NF libres  */
    OUT_MODE_12V,       /**< +12 V injecté sur le commun (via PTC)      */
    OUT_MODE_5V         /**< +5 V injecté sur le commun (via PTC)       */
} out_mode_t;

/** Fonction logique d'une sortie. */
typedef enum {
    OUT_FN_NONE = 0,
    OUT_FN_SIREN,       /**< Sirène : toutes alarmes sonores            */
    OUT_FN_SIREN_FIRE,  /**< Sirène incendie (modulée 1 s / 1 s)        */
    OUT_FN_STROBE,      /**< Flash : de l'alarme jusqu'à l'arrêt        */
    OUT_FN_ARMED,       /**< Recopie état armé                          */
    OUT_FN_FAULT,       /**< Recopie défaut système                     */
    OUT_FN_ZONE_FOLLOW, /**< Recopie de zones (masque)                  */
    OUT_FN_REMOTE,      /**< Télécommande (écran, SMS, réseau)          */
    OUT_FN_ENTRY_EXIT,  /**< Buzzer pendant les temporisations          */
    OUT_FN_PANIC,       /**< Report panique (transmetteur externe)      */
    OUT_FN_TECHNICAL    /**< Report alarme technique (électrovanne…)    */
} out_function_t;

/* ------------------------------------------------------------------------ */
/* Défauts système                                                          */
/* ------------------------------------------------------------------------ */
typedef enum {
    FAULT_MAINS = 0,        /**< Absence secteur                           */
    FAULT_BATTERY_LOW,      /**< Batterie basse                            */
    FAULT_BATTERY_MISSING,  /**< Batterie absente / déconnectée            */
    FAULT_AUX_FUSE,         /**< Fusible 12 V auxiliaire détecteurs        */
    FAULT_OUT_FUSE,         /**< Fusible bus d'alimentation des sorties    */
    FAULT_ZONE_REF,         /**< Référence VZONE hors tolérance            */
    FAULT_GSM,              /**< Pas de réseau mobile / SIM                */
    FAULT_ETH,              /**< Lien Ethernet absent                      */
    FAULT_RELAY_CHAIN,      /**< Chaîne registres relais défectueuse       */
    FAULT_BOX_TAMPER,       /**< Autoprotection coffret ouverte            */
    FAULT_RTC,              /**< Horloge non synchronisée                  */
    FAULT_COUNT
} fault_t;

/* ------------------------------------------------------------------------ */
/* Utilisateurs                                                             */
/* ------------------------------------------------------------------------ */
typedef enum {
    LEVEL_NONE = 0,
    LEVEL_USER,         /**< Armer / désarmer                          */
    LEVEL_MASTER,       /**< + gestion utilisateurs, inhibitions       */
    LEVEL_INSTALLER     /**< + configuration complète                  */
} user_level_t;

#ifdef __cplusplus
}
#endif
#endif /* ALARM_TYPES_H */
