/**
 * @file alarm_config.h
 * @brief Structure de configuration persistante (stockée en Flash QSPI).
 */
#ifndef ALARM_CONFIG_H
#define ALARM_CONFIG_H

#include "alarm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIG_MAGIC    0x414C5246u   /* "ALRF" */
#define CONFIG_VERSION  1u

typedef struct {
    char        name[NAME_LEN];
    uint8_t     type;           /**< zone_type_t                          */
    uint8_t     wiring;         /**< wiring_t                             */
    uint8_t     flags;          /**< ZF_xxx                               */
    uint8_t     reserved;
    uint16_t    response_ms;    /**< Temps de réponse (anti-rebond)       */
    uint16_t    voice_id;       /**< Message vocal du nom de zone (0=n°)  */
} zone_cfg_t;

typedef struct {
    char        name[NAME_LEN];
    uint8_t     mode;           /**< out_mode_t                           */
    uint8_t     function;       /**< out_function_t                       */
    uint8_t     invert;         /**< 1 = logique inversée                 */
    uint8_t     reserved;
    uint32_t    zone_mask;      /**< Pour OUT_FN_ZONE_FOLLOW              */
    uint16_t    pulse_s;        /**< 0 = maintenu, sinon impulsion (s)    */
    uint16_t    reserved2;
} output_cfg_t;

typedef struct {
    char        name[16];
    uint8_t     level;          /**< user_level_t                         */
    uint8_t     enabled;
    uint8_t     salt[8];
    uint8_t     hash[32];       /**< SHA-256(salt || PIN)                 */
} user_cfg_t;

/* Événements notifiés à un contact */
#define NOTIF_INTRUSION   (1u << 0)
#define NOTIF_FIRE        (1u << 1)
#define NOTIF_PANIC       (1u << 2)
#define NOTIF_TAMPER      (1u << 3)
#define NOTIF_TECHNICAL   (1u << 4)
#define NOTIF_FAULT       (1u << 5)
#define NOTIF_ARMING      (1u << 6)

typedef struct {
    char        name[16];
    char        phone[PHONE_LEN];
    uint8_t     sms;            /**< 1 = envoi SMS                        */
    uint8_t     call;           /**< 1 = appel vocal                      */
    uint8_t     remote_ctrl;    /**< 1 = autorisé à piloter par SMS       */
    uint8_t     reserved;
    uint16_t    event_mask;     /**< NOTIF_xxx                            */
    uint16_t    reserved2;
} contact_cfg_t;

typedef struct {
    uint16_t    short_max;      /**< ‰ : en dessous = court-circuit       */
    uint16_t    normal_max;     /**< ‰ : en dessous = repos (1 × EOL)     */
    uint16_t    alarm_max;      /**< ‰ : en dessous = alarme (2 × EOL)    */
    uint16_t    reserved;
} loop_thresholds_t;

typedef struct {
    uint8_t     dhcp;
    uint8_t     reserved[3];
    uint8_t     ip[4];
    uint8_t     mask[4];
    uint8_t     gateway[4];
    uint8_t     dns[4];
    char        ntp_server[32];
    uint16_t    http_port;
    uint16_t    reserved2;
} net_cfg_t;

typedef struct {
    uint32_t            magic;
    uint16_t            version;
    uint16_t            size;
    char                site_name[NAME_LEN];
    uint16_t            exit_delay_s;
    uint16_t            entry_delay_s;
    uint16_t            siren_duration_s;
    uint8_t             swinger_limit;      /**< Alarmes max par zone et par MES */
    uint8_t             volume;             /**< 0..100 %                        */
    uint16_t            mains_fail_delay_s; /**< Délai avant défaut secteur      */
    uint16_t            bat_low_mv;
    uint16_t            bat_ok_mv;
    uint8_t             voice_enabled;
    uint8_t             tamper_when_disarmed_siren; /**< 1 = sirène int. sur sabotage désarmé */
    int16_t             utc_offset_min;
    loop_thresholds_t   thresholds;
    zone_cfg_t          zones[NB_ZONES];
    output_cfg_t        outputs[NB_OUTPUTS];
    user_cfg_t          users[NB_USERS];
    contact_cfg_t       contacts[NB_CONTACTS];
    net_cfg_t           net;
    char                sim_pin[8];
    char                apn[32];
    uint32_t            crc32;              /**< Toujours en dernier             */
} alarm_config_t;

/** Remplit la configuration d'usine (code installateur 0000, maître 1234). */
void config_defaults(alarm_config_t *cfg);

/** Calcule et écrit le CRC. À appeler avant sauvegarde. */
void config_seal(alarm_config_t *cfg);

/** Vérifie magic, version, taille, CRC et bornes. */
bool config_is_valid(const alarm_config_t *cfg);

uint32_t crc32_compute(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* ALARM_CONFIG_H */
