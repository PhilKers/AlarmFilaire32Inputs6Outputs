/**
 * @file net_api.c
 * @brief Serveur HTTP/JSON minimal (lwIP netconn) pour supervision et
 *        commande depuis le réseau local, + synchronisation horaire SNTP.
 *
 *   GET  /api/etat          état, défauts, temporisation, tensions
 *   GET  /api/zones         32 zones : nom, type, état, mesure ‰, inhibition
 *   GET  /api/sorties       6 sorties : nom, mode, fonction, état
 *   GET  /api/journal?n=50  derniers événements
 *   POST /api/commande      corps texte identique aux SMS : "1234 MES"
 *
 * SÉCURITÉ : pas de TLS dans cette version. À n'exposer que sur le réseau
 * local ou derrière un VPN. Le verrouillage anti force brute est partagé
 * avec les SMS. Pour un accès Internet, ajouter mbedTLS (HTTPS) et un jeton.
 */
#include "app_main.h"
#include "lwip/api.h"
#include "lwip/apps/sntp.h"
#include "bsp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RESP_MAX 6144
static char resp[RESP_MAX];

static const char *const zone_state_txt[] = { "repos", "ouverte", "sabotage", "defaut" };
static const char *const out_mode_txt[]   = { "sec", "12V", "5V" };

static int json_str(char *o, size_t len, const char *s)
{
    /* Échappement minimal des guillemets et barres obliques inverses */
    size_t n = 0;
    if (n < len) o[n++] = '"';
    for (; *s && n + 3 < len; s++) {
        if (*s == '"' || *s == '\\') o[n++] = '\\';
        o[n++] = *s;
    }
    if (n < len) o[n++] = '"';
    if (n < len) o[n] = 0;
    return (int)n;
}

static int api_etat(char *o, size_t len)
{
    char s[256];
    core_lock();
    status_summary(&g_cfg, &g_app.engine, app_now(), s, sizeof s);
    const power_meas_t *p = &g_app.last_power;
    int n = snprintf(o, len, "{\"etat\":%d,\"mode\":%d,\"alarmes\":%u,\"defauts\":%lu,"
                     "\"tempo_s\":%lu,\"secteur\":%s,\"vbat_mv\":%u,\"ibat_ma\":%d,"
                     "\"gsm_dbm\":%d,\"resume\":",
                     g_app.engine.state, g_app.engine.mode, g_app.engine.active_alarms,
                     (unsigned long)g_app.engine.faults,
                     (unsigned long)engine_delay_remaining_s(&g_app.engine, app_now()),
                     p->ac_ok ? "true" : "false", p->vbat_mv, p->ibat_ma, g_comm.rssi_dbm);
    core_unlock();
    n += json_str(o + n, len - (size_t)n, s);
    n += snprintf(o + n, len - (size_t)n, "}");
    return n;
}

static int api_zones(char *o, size_t len)
{
    int n = snprintf(o, len, "[");
    core_lock();
    for (unsigned z = 0; z < NB_ZONES && (size_t)n < len - 128; z++) {
        n += snprintf(o + n, len - (size_t)n, "%s{\"n\":%u,\"nom\":", z ? "," : "", z + 1);
        n += json_str(o + n, len - (size_t)n, g_cfg.zones[z].name);
        n += snprintf(o + n, len - (size_t)n,
                      ",\"type\":%u,\"etat\":\"%s\",\"mesure\":%u,\"inhibee\":%s}",
                      g_cfg.zones[z].type, zone_state_txt[g_app.engine.zone[z]],
                      g_app.inputs.z[z].last_permille,
                      ((g_app.engine.bypass | g_app.engine.auto_bypass) >> z) & 1u ? "true" : "false");
    }
    core_unlock();
    n += snprintf(o + n, len - (size_t)n, "]");
    return n;
}

static int api_sorties(char *o, size_t len)
{
    int n = snprintf(o, len, "[");
    core_lock();
    for (unsigned i = 0; i < NB_OUTPUTS; i++) {
        n += snprintf(o + n, len - (size_t)n, "%s{\"n\":%u,\"nom\":", i ? "," : "", i + 1);
        n += json_str(o + n, len - (size_t)n, g_cfg.outputs[i].name);
        n += snprintf(o + n, len - (size_t)n, ",\"mode\":\"%s\",\"fonction\":%u,\"active\":%s}",
                      out_mode_txt[g_cfg.outputs[i].mode], g_cfg.outputs[i].function,
                      g_app.outputs.ch[i].main_on ? "true" : "false");
    }
    core_unlock();
    n += snprintf(o + n, len - (size_t)n, "]");
    return n;
}

static int api_journal(char *o, size_t len, unsigned count)
{
    int n = snprintf(o, len, "[");
    core_lock();
    alarm_event_t ev;
    for (uint32_t i = 0; i < count && event_log_get(&g_app.log, i, &ev) && (size_t)n < len - 200; i++) {
        char l[160];
        event_format(&ev, &g_cfg, l, sizeof l);
        n += snprintf(o + n, len - (size_t)n, "%s", i ? "," : "");
        n += json_str(o + n, len - (size_t)n, l);
    }
    core_unlock();
    n += snprintf(o + n, len - (size_t)n, "]");
    return n;
}

static void send_response(struct netconn *c, int code, const char *type, const char *body, int blen)
{
    char hdr[160];
    int h = snprintf(hdr, sizeof hdr,
                     "HTTP/1.1 %d %s\r\nContent-Type: %s; charset=utf-8\r\n"
                     "Content-Length: %d\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
                     code, code == 200 ? "OK" : code == 403 ? "Forbidden" : "Not Found", type, blen);
    netconn_write(c, hdr, (size_t)h, NETCONN_COPY);
    netconn_write(c, body, (size_t)blen, NETCONN_COPY);
}

static void serve(struct netconn *c)
{
    struct netbuf *nb;
    if (netconn_recv(c, &nb) != ERR_OK) return;
    char req[512];
    u16_t l = netbuf_copy(nb, req, sizeof req - 1);
    req[l] = 0;
    netbuf_delete(nb);

    int n;
    if (!strncmp(req, "GET /api/etat", 13))          n = api_etat(resp, RESP_MAX);
    else if (!strncmp(req, "GET /api/zones", 14))    n = api_zones(resp, RESP_MAX);
    else if (!strncmp(req, "GET /api/sorties", 16))  n = api_sorties(resp, RESP_MAX);
    else if (!strncmp(req, "GET /api/journal", 16)) {
        const char *q = strstr(req, "n=");
        unsigned cnt = q ? (unsigned)atoi(q + 2) : 50u;
        n = api_journal(resp, RESP_MAX, cnt > 200 ? 200 : cnt);
    } else if (!strncmp(req, "POST /api/commande", 18)) {
        const char *body = strstr(req, "\r\n\r\n");
        char rep[200];
        bool ok = false;
        if (body) {
            core_lock();
            ok = alarm_app_remote(&g_app, body + 4, ORIGIN_NETWORK, app_now(), rep, sizeof rep);
            core_unlock();
        } else {
            strcpy(rep, "Corps absent");
        }
        n = snprintf(resp, RESP_MAX, "{\"ok\":%s,\"reponse\":", ok ? "true" : "false");
        n += json_str(resp + n, RESP_MAX - (size_t)n, rep);
        n += snprintf(resp + n, RESP_MAX - (size_t)n, "}");
        send_response(c, ok ? 200 : 403, "application/json", resp, n);
        return;
    } else {
        n = snprintf(resp, RESP_MAX, "{\"erreur\":\"inconnu\"}");
        send_response(c, 404, "application/json", resp, n);
        return;
    }
    send_response(c, 200, "application/json", resp, n);
}

/* Appelée par lwIP (SNTP_SET_SYSTEM_TIME dans lwipopts.h) */
void app_sntp_set_time(uint32_t sec)
{
    bsp_rtc_set_unix(sec);
    core_lock();
    engine_set_fault(&g_app.engine, FAULT_RTC, false, app_now());
    core_unlock();
}

void net_task(void *arg)
{
    (void)arg;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, g_cfg.net.ntp_server);
    sntp_init();

    struct netconn *srv = netconn_new(NETCONN_TCP);
    netconn_bind(srv, IP_ADDR_ANY, g_cfg.net.http_port ? g_cfg.net.http_port : 80);
    netconn_listen(srv);
    for (;;) {
        struct netconn *c;
        if (netconn_accept(srv, &c) == ERR_OK) {
            netconn_set_recvtimeout(c, 3000);
            serve(c);
            netconn_close(c);
            netconn_delete(c);
        }
    }
}
