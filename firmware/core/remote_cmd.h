/**
 * @file remote_cmd.h
 * @brief Commandes texte à distance (SMS, API réseau).
 *
 * Syntaxe : "<code> <commande> [arguments]" (insensible à la casse)
 *   1234 MES | ARMER | TOTAL        mise en service totale
 *   1234 MESP | PARTIEL             mise en service partielle
 *   1234 MHS | DESARMER | ARRET     mise hors service / arrêt sirènes
 *   1234 ETAT                       état, défauts, zones ouvertes
 *   1234 SORTIE <1-6> ON|OFF        sortie configurée en « télécommande »
 */
#ifndef REMOTE_CMD_H
#define REMOTE_CMD_H
#include "alarm_engine.h"
#include "outputs.h"
#include "users.h"

typedef enum {
    RCMD_INVALID = 0,
    RCMD_ARM_AWAY,
    RCMD_ARM_STAY,
    RCMD_DISARM,
    RCMD_STATUS,
    RCMD_OUTPUT
} rcmd_action_t;

typedef struct {
    char          pin[PIN_MAX_LEN + 1];
    rcmd_action_t action;
    uint8_t       output;   /**< 0..5 */
    bool          on;
} rcmd_t;

bool remote_cmd_parse(const char *text, rcmd_t *cmd);

typedef struct {
    const alarm_config_t *cfg;
    alarm_engine_t       *engine;
    outputs_t            *outputs;
    auth_guard_t         *guard;
} remote_ctx_t;

/**
 * Analyse, authentifie et exécute une commande. Écrit la réponse (SMS) dans
 * reply. Retourne true si la commande a été exécutée.
 */
bool remote_cmd_execute(remote_ctx_t *rc, const char *text, origin_t origin, ms_t now,
                        char *reply, size_t reply_len);

/** Résumé d'état (réponse à ETAT, écran, API). */
int  status_summary(const alarm_config_t *cfg, const alarm_engine_t *e, ms_t now,
                    char *out, size_t len);
#endif
