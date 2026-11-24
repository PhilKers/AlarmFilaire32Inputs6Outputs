/**
 * @file users.h
 * @brief Gestion des codes d'accès : hachage salé, vérification, anti-force brute.
 */
#ifndef USERS_H
#define USERS_H
#include "alarm_config.h"

#define PIN_MIN_LEN         4u
#define PIN_MAX_LEN         8u
#define AUTH_MAX_FAILS      5u          /**< Essais avant verrouillage       */
#define AUTH_LOCKOUT_MS     (180u * 1000u)  /**< Verrouillage 3 minutes      */

typedef struct {
    uint8_t fails;
    bool    locked;
    ms_t    locked_until;
} auth_guard_t;

typedef enum {
    AUTH_OK = 0,
    AUTH_BAD_CODE,
    AUTH_LOCKED,
    AUTH_INVALID_FORMAT
} auth_result_t;

/** Définit le code d'un utilisateur. salt : 8 octets aléatoires (RNG matériel). */
bool user_set_pin(user_cfg_t *u, const char *pin, const uint8_t salt[8]);

/**
 * Vérifie un code contre tous les utilisateurs actifs.
 * @param[out] user_index  index de l'utilisateur reconnu (si AUTH_OK)
 */
auth_result_t users_authenticate(const alarm_config_t *cfg, auth_guard_t *g,
                                 const char *pin, ms_t now, int *user_index);

/** Format valide : 4 à 8 chiffres. */
bool pin_format_ok(const char *pin);
#endif
