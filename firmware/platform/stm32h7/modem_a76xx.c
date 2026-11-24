/**
 * @file modem_a76xx.c
 * @brief Pilote du module 4G SIMCom A7670E (LTE Cat-1 + repli 2G).
 *
 * Liaison : USART3 115200 bauds avec RTS/CTS, via traducteur TXS0108E
 * (3,3 V ↔ 1,8 V). Le module est alimenté par un régulateur 3,8 V dédié
 * commandable (MODEM_PWR_EN) pour un redémarrage « dur » en cas de blocage.
 *
 * Fonctions :
 *   - surveillance SIM / enregistrement réseau / niveau de signal
 *   - envoi des SMS et appels vocaux en cascade de la file notifier_t
 *   - lecture des messages vocaux pendant l'appel (fichiers AMR stockés
 *     dans le système de fichiers du module, mêmes numéros que /VOIX/)
 *   - acquittement et commandes DTMF, commandes par SMS entrant
 *
 * NOTE : vérifier la syntaxe exacte de AT+CCMXPLAY et AT+DDET dans le
 * « A76XX Series AT Command Manual » correspondant au firmware du module.
 */
#include "app_main.h"
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern UART_HandleTypeDef huart3;

#define MODEM_PLAY_FMT      "AT+CCMXPLAY=\"C:/%s\",1,0"   /* 1 = vers l'interlocuteur */
#define CALL_ANSWER_TMO_MS  45000u
#define DTMF_WAIT_MS        20000u
#define NET_LOST_FAULT_MS   120000u

static StreamBufferHandle_t rx_sb;
static uint8_t rx_dma[256] __attribute__((section(".dma_buffer")));

/* URC mémorisés pendant l'attente d'une réponse */
static volatile int  pending_sms_index = -1;
static volatile bool ring_pending;
static char          clip_number[PHONE_LEN];
static char          dtmf_buf[16];
static volatile unsigned dtmf_len;
static volatile bool call_ended;

/* ---------------------------------------------------------------------- */
/* Réception                                                              */
/* ---------------------------------------------------------------------- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *h, uint16_t size)
{
    if (h != &huart3) return;
    BaseType_t w = pdFALSE;
    xStreamBufferSendFromISR(rx_sb, rx_dma, size, &w);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma, sizeof rx_dma);
    portYIELD_FROM_ISR(w);
}

/** Lit une ligne (sans CR/LF). Le prompt "> " de AT+CMGS compte comme une ligne. */
static bool read_line(char *buf, size_t len, uint32_t tmo_ms)
{
    size_t n = 0;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(tmo_ms);
    while (xTaskGetTickCount() < end) {
        char c;
        if (xStreamBufferReceive(rx_sb, &c, 1, pdMS_TO_TICKS(20)) != 1) continue;
        if (c == '\r') continue;
        if (c == '\n') {
            if (n == 0) continue;
            buf[n] = 0;
            return true;
        }
        if (n + 1 < len) buf[n++] = c;
        if (n == 2 && buf[0] == '>' && buf[1] == ' ') { buf[n] = 0; return true; }
    }
    buf[n] = 0;
    return false;
}

static void send_raw(const char *s)
{
    HAL_UART_Transmit(&huart3, (const uint8_t *)s, (uint16_t)strlen(s), 1000);
}

/** Traite les messages non sollicités. Retourne true si la ligne en était un. */
static bool handle_urc(const char *l)
{
    if (!strncmp(l, "+CMTI:", 6)) {
        const char *p = strchr(l, ',');
        if (p) pending_sms_index = atoi(p + 1);
        return true;
    }
    if (!strcmp(l, "RING")) { ring_pending = true; return true; }
    if (!strncmp(l, "+CLIP:", 6)) {
        const char *a = strchr(l, '"');
        const char *b = a ? strchr(a + 1, '"') : NULL;
        if (a && b && (size_t)(b - a - 1) < sizeof clip_number) {
            memcpy(clip_number, a + 1, (size_t)(b - a - 1));
            clip_number[b - a - 1] = 0;
        }
        return true;
    }
    if (!strncmp(l, "+RXDTMF:", 8)) {
        char d = l[8] == ' ' ? l[9] : l[8];
        if (dtmf_len + 1 < sizeof dtmf_buf) { dtmf_buf[dtmf_len++] = d; dtmf_buf[dtmf_len] = 0; }
        return true;
    }
    if (!strcmp(l, "NO CARRIER") || !strcmp(l, "VOICE CALL: END") || !strncmp(l, "BUSY", 4) ||
        !strncmp(l, "NO ANSWER", 9)) {
        call_ended = true;
        return true;
    }
    return false;
}

/**
 * Envoie une commande AT et attend `expect` (ou OK). Recopie la première
 * ligne de réponse informative dans resp.
 */
static bool at_cmd(const char *cmd, const char *expect, uint32_t tmo_ms, char *resp, size_t rlen)
{
    char line[200];
    if (resp && rlen) resp[0] = 0;
    xStreamBufferReset(rx_sb);
    send_raw(cmd);
    send_raw("\r");
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(tmo_ms);
    while (xTaskGetTickCount() < end) {
        if (!read_line(line, sizeof line, 100)) continue;
        if (!strcmp(line, cmd)) continue;                 /* écho éventuel */
        if (expect && !strncmp(line, expect, strlen(expect))) {
            if (resp && !resp[0]) strncpy(resp, line, rlen - 1);
            if (strcmp(expect, "OK") && strcmp(expect, "> ")) {
                /* Réponse informative : attendre aussi le OK final */
                char ok[16];
                read_line(ok, sizeof ok, 500);
            }
            return true;
        }
        if (!strcmp(line, "OK")) return expect == NULL;
        if (!strncmp(line, "ERROR", 5) || !strncmp(line, "+CME ERROR", 10) ||
            !strncmp(line, "+CMS ERROR", 10)) return false;
        if (!handle_urc(line) && resp && !resp[0]) strncpy(resp, line, rlen - 1);
    }
    return false;
}

static bool at_ok(const char *cmd, uint32_t tmo_ms) { return at_cmd(cmd, "OK", tmo_ms, NULL, 0); }

/* ---------------------------------------------------------------------- */
/* Alimentation et initialisation                                         */
/* ---------------------------------------------------------------------- */
static void modem_power_cycle(void)
{
    HAL_GPIO_WritePin(MODEM_PWR_EN_PORT, MODEM_PWR_EN_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(2000));
    HAL_GPIO_WritePin(MODEM_PWR_EN_PORT, MODEM_PWR_EN_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(100));
    /* Impulsion PWRKEY (transistor : niveau haut = PWRKEY tiré à la masse) */
    HAL_GPIO_WritePin(MODEM_PWRKEY_PORT, MODEM_PWRKEY_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(500));
    HAL_GPIO_WritePin(MODEM_PWRKEY_PORT, MODEM_PWRKEY_PIN, GPIO_PIN_RESET);
    for (int i = 0; i < 30 && HAL_GPIO_ReadPin(MODEM_STATUS_PORT, MODEM_STATUS_PIN) == GPIO_PIN_RESET; i++)
        vTaskDelay(pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(3000));
}

static bool modem_configure(void)
{
    for (int i = 0; i < 10; i++) {
        if (at_ok("AT", 500)) break;
        if (i == 9) return false;
    }
    at_ok("ATE0", 500);
    at_ok("AT+CMEE=2", 500);
    char r[64];
    if (!at_cmd("AT+CPIN?", "+CPIN:", 5000, r, sizeof r)) return false;
    if (strstr(r, "SIM PIN") && g_cfg.sim_pin[0]) {
        char c[32];
        snprintf(c, sizeof c, "AT+CPIN=\"%s\"", g_cfg.sim_pin);
        if (!at_ok(c, 5000)) return false;      /* ne jamais réessayer : risque PUK */
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    g_comm.sim_ready = true;
    at_ok("AT+CMGF=1", 1000);                   /* SMS en mode texte          */
    at_ok("AT+CSCS=\"GSM\"", 1000);
    at_ok("AT+CNMI=2,1,0,0,0", 1000);           /* URC +CMTI sur SMS reçu     */
    at_ok("AT+CLIP=1", 1000);                   /* présentation du numéro     */
    at_ok("AT+DDET=1", 1000);                   /* détection DTMF → +RXDTMF   */
    if (g_cfg.apn[0]) {
        char c[64];
        snprintf(c, sizeof c, "AT+CGDCONT=1,\"IP\",\"%s\"", g_cfg.apn);
        at_ok(c, 2000);
    }
    return true;
}

static void modem_poll_network(void)
{
    char r[64];
    if (at_cmd("AT+CSQ", "+CSQ:", 1000, r, sizeof r)) {
        int rssi = atoi(r + 6);
        g_comm.rssi_dbm = (rssi >= 0 && rssi <= 31) ? (int8_t)(-113 + 2 * rssi) : 0;
    }
    bool reg = false;
    if (at_cmd("AT+CEREG?", "+CEREG:", 1000, r, sizeof r)) {
        const char *p = strchr(r, ',');
        reg = p && (p[1] == '1' || p[1] == '5');
    }
    if (!reg && at_cmd("AT+CREG?", "+CREG:", 1000, r, sizeof r)) {
        const char *p = strchr(r, ',');
        reg = p && (p[1] == '1' || p[1] == '5');
    }
    g_comm.registered = reg;
}

/* ---------------------------------------------------------------------- */
/* SMS                                                                    */
/* ---------------------------------------------------------------------- */
static bool sms_send(const char *number, const char *text)
{
    char c[48];
    snprintf(c, sizeof c, "AT+CMGS=\"%s\"", number);
    if (!at_cmd(c, "> ", 5000, NULL, 0)) return false;
    send_raw(text);
    char ctrlz[2] = { 0x1A, 0 };
    return at_cmd(ctrlz, "+CMGS:", 60000, NULL, 0);
}

static int contact_by_number(const char *num)
{
    size_t ln = strlen(num);
    for (unsigned i = 0; i < NB_CONTACTS; i++) {
        const char *p = g_cfg.contacts[i].phone;
        size_t lp = strlen(p);
        /* Comparaison sur les 9 derniers chiffres (+33 6… ≡ 06…) */
        if (lp >= 9 && ln >= 9 && !strcmp(p + lp - 9, num + ln - 9)) return (int)i;
    }
    return -1;
}

static void sms_process_incoming(int idx)
{
    char c[24], hdr[160] = "", body[161], reply[160];
    snprintf(c, sizeof c, "AT+CMGR=%d", idx);
    /* Réponse sur plusieurs lignes : en-tête +CMGR, texte, OK */
    xStreamBufferReset(rx_sb);
    send_raw(c); send_raw("\r");
    char line[200];
    bool got_hdr = false;
    body[0] = 0;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(3000);
    while (xTaskGetTickCount() < end && read_line(line, sizeof line, 500)) {
        if (!strncmp(line, "+CMGR:", 6)) { strncpy(hdr, line, sizeof hdr - 1); got_hdr = true; continue; }
        if (!strcmp(line, "OK")) break;
        if (got_hdr && !body[0]) strncpy(body, line, sizeof body - 1);
    }
    /* Expéditeur : 2e champ entre guillemets */
    char sender[PHONE_LEN] = "";
    const char *a = strchr(hdr, ',');
    if (a && a[1] == '"') {
        const char *b = strchr(a + 2, '"');
        if (b && (size_t)(b - a - 2) < sizeof sender) {
            memcpy(sender, a + 2, (size_t)(b - a - 2));
            sender[b - a - 2] = 0;
        }
    }
    int ci = contact_by_number(sender);
    if (ci >= 0 && g_cfg.contacts[ci].remote_ctrl) {
        core_lock();
        alarm_app_remote(&g_app, body, ORIGIN_SMS, app_now(), reply, sizeof reply);
        core_unlock();
        sms_send(sender, reply);
    }
    /* Numéro inconnu : SMS ignoré (aucune réponse, pas d'information divulguée) */
    snprintf(c, sizeof c, "AT+CMGD=%d", idx);
    at_ok(c, 5000);
}

/* ---------------------------------------------------------------------- */
/* Appels vocaux                                                          */
/* ---------------------------------------------------------------------- */
static void call_play(const uint16_t *seq, unsigned n)
{
    for (unsigned i = 0; i < n && !call_ended; i++) {
        char wav[24], amr[24], c[64];
        voice_filename(seq[i], wav, sizeof wav);            /* /VOIX/M006.WAV */
        const char *base = strrchr(wav, '/') + 1;
        snprintf(amr, sizeof amr, "%.*s.AMR", (int)(strchr(base, '.') - base), base);
        snprintf(c, sizeof c, MODEM_PLAY_FMT, amr);
        at_ok(c, 2000);
        vTaskDelay(pdMS_TO_TICKS(1800));                    /* durée moyenne d'un fragment */
    }
}

/** Attend un code DTMF terminé par '#', ou une touche seule. */
static char dtmf_wait(char *code, size_t len, uint32_t tmo)
{
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(tmo);
    char line[64];
    while (xTaskGetTickCount() < end && !call_ended) {
        if (read_line(line, sizeof line, 100)) handle_urc(line);
        if (dtmf_len == 0) continue;
        char last = dtmf_buf[dtmf_len - 1];
        if (dtmf_len == 1 && (last == '1' || last == '2')) { dtmf_len = 0; return last; }
        if (last == '#') {
            dtmf_buf[dtmf_len - 1] = 0;
            strncpy(code, dtmf_buf, len - 1);
            code[len - 1] = 0;
            dtmf_len = 0;
            return '#';
        }
    }
    return 0;
}

/** Appel d'alarme. Retourne true si le correspondant a acquitté (touche 1). */
static bool call_alarm(const char *number, const alarm_event_t *ev)
{
    char c[48];
    call_ended = false;
    dtmf_len = 0;
    snprintf(c, sizeof c, "ATD%s;", number);
    if (!at_ok(c, 5000)) return false;

    /* Attente du décroché : état 0 (actif) dans +CLCC */
    bool answered = false;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(CALL_ANSWER_TMO_MS);
    while (!answered && !call_ended && xTaskGetTickCount() < end) {
        char r[96];
        if (at_cmd("AT+CLCC", "+CLCC:", 1000, r, sizeof r) && strstr(r, ",0,0,0,")) answered = true;
        else vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!answered) { at_ok("AT+CHUP", 2000); return false; }

    uint16_t seq[VOICE_MAX_SEQ + 2];
    unsigned n = 0;
    seq[n++] = VM_CALL_INTRO;
    n += voice_for_event(&g_cfg, ev, false, &seq[n], VOICE_MAX_SEQ);
    bool ack = false;
    for (int attempt = 0; attempt < 3 && !ack && !call_ended; attempt++) {
        call_play(seq, n);
        uint16_t menu = VM_CALL_MENU;
        call_play(&menu, 1);
        char code[12] = "";
        char k = dtmf_wait(code, sizeof code, DTMF_WAIT_MS);
        if (k == '1') ack = true;
        else if (k == '#') {
            /* "9<code>#" : mise hors service à distance */
            if (code[0] == '9') {
                char cmd[24];
                snprintf(cmd, sizeof cmd, "%s MHS", code + 1);
                char rep[96];
                core_lock();
                ack = alarm_app_remote(&g_app, cmd, ORIGIN_PHONE_DTMF, app_now(), rep, sizeof rep);
                core_unlock();
            }
        }
    }
    if (ack) { uint16_t m = VM_ACK; call_play(&m, 1); }
    at_ok("AT+CHUP", 2000);
    return ack;
}

static void incoming_call(void)
{
    ring_pending = false;
    int ci = contact_by_number(clip_number);
    if (ci < 0 || !g_cfg.contacts[ci].remote_ctrl) { at_ok("AT+CHUP", 2000); return; }
    call_ended = false;
    dtmf_len = 0;
    if (!at_ok("ATA", 5000)) return;
    /* Annonce de l'état puis attente de "code#" suivi de 1 (MES) ou 9 (MHS) */
    uint16_t m;
    core_lock();
    m = (g_app.engine.state == SYS_DISARMED) ? VM_DISARMED : VM_ARMED_AWAY;
    core_unlock();
    call_play(&m, 1);
    char code[12] = "";
    if (dtmf_wait(code, sizeof code, DTMF_WAIT_MS) == '#' && strlen(code) > 4) {
        char act = code[strlen(code) - 1];
        code[strlen(code) - 1] = 0;
        char cmd[24], rep[96];
        snprintf(cmd, sizeof cmd, "%s %s", code, act == '1' ? "MES" : act == '9' ? "MHS" : "ETAT");
        core_lock();
        bool ok = alarm_app_remote(&g_app, cmd, ORIGIN_PHONE_DTMF, app_now(), rep, sizeof rep);
        core_unlock();
        m = ok ? VM_ACK : VM_BAD_CODE;
        call_play(&m, 1);
    }
    at_ok("AT+CHUP", 2000);
}

/* ---------------------------------------------------------------------- */
/* Tâche                                                                  */
/* ---------------------------------------------------------------------- */
void modem_task(void *arg)
{
    (void)arg;
    rx_sb = xStreamBufferCreate(1024, 1);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart3, rx_dma, sizeof rx_dma);

    ms_t last_reg_ok = app_now();
    TickType_t last_poll = 0;
    for (;;) {
        modem_power_cycle();
        if (!modem_configure()) {
            core_lock(); engine_set_fault(&g_app.engine, FAULT_GSM, true, app_now()); core_unlock();
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }
        int consecutive_errors = 0;
        while (consecutive_errors < 5) {
            char line[200];
            if (read_line(line, sizeof line, 200)) handle_urc(line);

            if (xTaskGetTickCount() - last_poll > pdMS_TO_TICKS(30000)) {
                last_poll = xTaskGetTickCount();
                modem_poll_network();
                consecutive_errors = at_ok("AT", 1000) ? 0 : consecutive_errors + 1;
                if (g_comm.registered) last_reg_ok = app_now();
                core_lock();
                engine_set_fault(&g_app.engine, FAULT_GSM,
                                 !g_comm.registered && time_reached(app_now(), last_reg_ok + NET_LOST_FAULT_MS),
                                 app_now());
                core_unlock();
            }
            if (pending_sms_index >= 0) {
                int i = pending_sms_index;
                pending_sms_index = -1;
                sms_process_incoming(i);
            }
            if (ring_pending) incoming_call();

            /* File de notifications */
            notif_job_t job;
            bool have = false;
            core_lock();
            const notif_job_t *j = notifier_peek(&g_app.notifier);
            if (j) { job = *j; have = true; }
            core_unlock();
            if (have && g_comm.registered) {
                const contact_cfg_t *c = &g_cfg.contacts[job.contact];
                bool ok;
                if (job.kind == JOB_SMS) {
                    char t[160], line2[140];
                    event_format(&job.ev, &g_cfg, line2, sizeof line2);
                    snprintf(t, sizeof t, "%s: %s", g_cfg.site_name, line2);
                    ok = sms_send(c->phone, t);
                } else {
                    ok = call_alarm(c->phone, &job.ev);
                }
                core_lock();
                notifier_done(&g_app.notifier, &g_cfg, ok);
                core_unlock();
            }
        }
        /* Module muet : redémarrage complet */
        g_comm.registered = false;
    }
}
