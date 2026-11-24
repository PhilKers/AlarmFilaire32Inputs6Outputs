/**
 * @file remote_cmd.c
 */
#include "remote_cmd.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *next_token(const char *p, char *tok, size_t len)
{
    while (*p && isspace((unsigned char)*p)) p++;
    size_t n = 0;
    while (*p && !isspace((unsigned char)*p)) {
        if (n + 1 < len) tok[n++] = (char)toupper((unsigned char)*p);
        p++;
    }
    tok[n] = 0;
    return p;
}

static bool is_one_of(const char *tok, const char *const *list)
{
    for (; *list; list++)
        if (strcmp(tok, *list) == 0) return true;
    return false;
}

bool remote_cmd_parse(const char *text, rcmd_t *cmd)
{
    static const char *const k_away[]   = { "MES", "ARMER", "TOTAL", "ON", 0 };
    static const char *const k_stay[]   = { "MESP", "PARTIEL", 0 };
    static const char *const k_disarm[] = { "MHS", "DESARMER", "ARRET", "OFF", 0 };
    static const char *const k_status[] = { "ETAT", "STATUS", "?", 0 };
    char tok[16];

    memset(cmd, 0, sizeof *cmd);
    cmd->action = RCMD_INVALID;
    const char *p = next_token(text, tok, sizeof tok);
    if (!pin_format_ok(tok)) return false;
    strcpy(cmd->pin, tok);

    p = next_token(p, tok, sizeof tok);
    if (is_one_of(tok, k_away))        cmd->action = RCMD_ARM_AWAY;
    else if (is_one_of(tok, k_stay))   cmd->action = RCMD_ARM_STAY;
    else if (is_one_of(tok, k_disarm)) cmd->action = RCMD_DISARM;
    else if (is_one_of(tok, k_status)) cmd->action = RCMD_STATUS;
    else if (strcmp(tok, "SORTIE") == 0 || strcmp(tok, "S") == 0) {
        p = next_token(p, tok, sizeof tok);
        int n = (tok[0] >= '1' && tok[0] <= '6' && tok[1] == 0) ? tok[0] - '1' : -1;
        if (n < 0) return false;
        next_token(p, tok, sizeof tok);
        if (strcmp(tok, "ON") == 0 || strcmp(tok, "1") == 0)        cmd->on = true;
        else if (strcmp(tok, "OFF") == 0 || strcmp(tok, "0") == 0)  cmd->on = false;
        else return false;
        cmd->output = (uint8_t)n;
        cmd->action = RCMD_OUTPUT;
    }
    return cmd->action != RCMD_INVALID;
}

int status_summary(const alarm_config_t *cfg, const alarm_engine_t *e, ms_t now,
                   char *out, size_t len)
{
    int n = snprintf(out, len, "%s: %s", cfg->site_name, engine_state_label(e));
    uint32_t rem = engine_delay_remaining_s(e, now);
    if (rem && (size_t)n < len) n += snprintf(out + n, len - (size_t)n, " (%lus)",
                                              (unsigned long)rem);
    if (e->faults && (size_t)n < len) {
        n += snprintf(out + n, len - (size_t)n, ". Defauts:");
        for (unsigned f = 0; f < FAULT_COUNT && (size_t)n < len; f++)
            if (e->faults & (1u << f))
                n += snprintf(out + n, len - (size_t)n, " %s;", fault_label((fault_t)f));
    }
    bool first = true;
    for (unsigned z = 0; z < NB_ZONES && (size_t)n < len; z++) {
        if (e->cfg->zones[z].type == ZT_DISABLED || e->zone[z] == ZSTATE_REST) continue;
        n += snprintf(out + n, len - (size_t)n, "%s%u", first ? ". Zones ouvertes: " : ",", z + 1);
        first = false;
    }
    if (e->alarm_memory && (size_t)n < len) {
        first = true;
        for (unsigned z = 0; z < NB_ZONES && (size_t)n < len; z++)
            if (e->alarm_memory & (1u << z)) {
                n += snprintf(out + n, len - (size_t)n, "%s%u", first ? ". Memoire: " : ",", z + 1);
                first = false;
            }
    }
    return n;
}

bool remote_cmd_execute(remote_ctx_t *rc, const char *text, origin_t origin, ms_t now,
                        char *reply, size_t reply_len)
{
    rcmd_t cmd;
    if (!remote_cmd_parse(text, &cmd)) {
        snprintf(reply, reply_len, "Commande inconnue. Ex: 1234 ETAT / MES / MESP / MHS / SORTIE 6 ON");
        return false;
    }
    int user = -1;
    auth_result_t ar = users_authenticate(rc->cfg, rc->guard, cmd.pin, now, &user);
    if (ar == AUTH_LOCKED) {
        snprintf(reply, reply_len, "Acces verrouille, reessayez plus tard.");
        return false;
    }
    if (ar != AUTH_OK) {
        alarm_event_t ev = { 0 };
        ev.type = EV_BAD_CODE; ev.source = SRC_NONE; ev.user = USER_NONE; ev.origin = (uint8_t)origin;
        if (rc->engine->cb) rc->engine->cb(rc->engine->cb_ctx, &ev);
        snprintf(reply, reply_len, "Code errone.");
        return false;
    }

    uint32_t open = 0;
    switch (cmd.action) {
    case RCMD_ARM_AWAY:
    case RCMD_ARM_STAY: {
        arm_result_t r = engine_arm(rc->engine, cmd.action == RCMD_ARM_AWAY ? ARM_AWAY : ARM_STAY,
                                    false, (uint8_t)user, origin, now, &open);
        if (r == ARM_OK) break;
        if (r == ARM_ALREADY) { snprintf(reply, reply_len, "Deja en service."); return false; }
        int n = snprintf(reply, reply_len, "MES refusee (%s):",
                         r == ARM_TAMPER_PRESENT ? "sabotage" : "zones ouvertes");
        for (unsigned z = 0; z < NB_ZONES && (size_t)n < reply_len; z++)
            if (open & (1u << z)) n += snprintf(reply + n, reply_len - (size_t)n, " %u", z + 1);
        return false;
    }
    case RCMD_DISARM:
        engine_disarm(rc->engine, (uint8_t)user, origin, now);
        break;
    case RCMD_OUTPUT:
        if (rc->cfg->outputs[cmd.output].function != OUT_FN_REMOTE) {
            snprintf(reply, reply_len, "Sortie %u non telecommandable.", cmd.output + 1u);
            return false;
        }
        outputs_set_remote(rc->outputs, cmd.output, cmd.on);
        if (rc->engine->cb) {
            alarm_event_t ev = { 0 };
            ev.type = cmd.on ? EV_OUTPUT_ON : EV_OUTPUT_OFF;
            ev.source = cmd.output; ev.user = (uint8_t)user; ev.origin = (uint8_t)origin;
            rc->engine->cb(rc->engine->cb_ctx, &ev);
        }
        break;
    case RCMD_STATUS:
    default:
        break;
    }
    status_summary(rc->cfg, rc->engine, now, reply, reply_len);
    return true;
}
