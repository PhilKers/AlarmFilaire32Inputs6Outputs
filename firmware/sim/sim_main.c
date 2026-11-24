/**
 * @file sim_main.c
 * @brief Simulateur console du noyau : permet de jouer des scénarios sans
 *        matériel. Commandes (une par ligne) :
 *   z <n> repos|ouvert|court|coupe   état électrique de la boucle n (1-32)
 *   mes <code> | mesp <code> | mhs <code>
 *   sms <texte>                      ex : sms 1234 ETAT
 *   secteur 0|1                      présence secteur
 *   att <secondes>                   fait avancer le temps
 *   etat | journal | quitter
 */
#include "alarm_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static alarm_config_t cfg;
static alarm_app_t app;
static uint16_t loops[NB_ZONES];
static ms_t now_ms;
static power_meas_t pm = { true, 13700, 13600, 150, 5000, 13500, 13500, 3300 };

static uint32_t sim_unix(void) { return 1795000000u + now_ms / 1000u; }
static void sim_voice(const uint16_t *s, unsigned n)
{
    printf("  [VOIX] ");
    for (unsigned i = 0; i < n; i++) {
        if (s[i] >= 0x2000u)      printf("<nom zone %u> ", s[i] - 0x2000u);
        else if (s[i] >= 0x1000u) printf("%u ", s[i] - 0x1000u);
        else                      printf("%s ", voice_texts[s[i]]);
    }
    printf("\n");
}
static void sim_persist(const alarm_event_t *ev)
{
    char line[160];
    event_format(ev, &cfg, line, sizeof line);
    printf("  [EVT] %s\n", line);
}

static void advance(ms_t dur)
{
    static uint32_t last_word = 0;
    for (ms_t end = now_ms + dur; now_ms < end; now_ms += 10) {
        alarm_app_zones(&app, loops, now_ms);
        if (now_ms % 1000 == 0) alarm_app_power(&app, &pm, now_ms);
        uint32_t w = alarm_app_tick(&app, now_ms);
        uint32_t relays = w & 0x3FFFFu;
        if (relays != (last_word & 0x3FFFFu)) {
            printf("  [RELAIS] ");
            for (unsigned i = 0; i < NB_OUTPUTS; i++) {
                const char *m = (w & RLY_SRC(i)) ? ((w & RLY_VSEL(i)) ? "5V" : "12V") : "sec";
                printf("S%u:%s/%s ", i + 1, (w & RLY_MAIN(i)) ? "ON" : "off", m);
            }
            printf("\n");
        }
        last_word = w;
        const notif_job_t *j;
        while ((j = notifier_peek(&app.notifier)) != NULL) {
            char t[160];
            event_format(&j->ev, &cfg, t, sizeof t);
            printf("  [4G] %s vers %s : %s\n", j->kind == JOB_SMS ? "SMS" : "APPEL",
                   cfg.contacts[j->contact].phone, t);
            notifier_done(&app.notifier, &cfg, true);
        }
    }
}

int main(void)
{
    config_defaults(&cfg);
    cfg.zones[1].type = ZT_FOLLOWER; strcpy(cfg.zones[1].name, "Couloir");
    cfg.zones[2].type = ZT_INSTANT;  strcpy(cfg.zones[2].name, "Salon");
    cfg.zones[2].flags |= ZF_PARTIAL;
    cfg.zones[3].type = ZT_FIRE;     strcpy(cfg.zones[3].name, "Detecteur fumee");
    strcpy(cfg.contacts[0].phone, "+33612345678");
    cfg.contacts[0].sms = 1; cfg.contacts[0].call = 1; cfg.contacts[0].event_mask = 0xFFFF;
    config_seal(&cfg);
    for (unsigned i = 0; i < NB_ZONES; i++) loops[i] = 524;

    app_hooks_t h = { sim_unix, sim_voice, sim_persist, NULL };
    alarm_app_init(&app, &cfg, &h);
    advance(1000);
    printf("Simulateur centrale 32 zones. Tapez 'aide'.\n> ");

    char line[200];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        char a1[32] = "", a2[32] = "";
        int n = 0;
        if (sscanf(line, "z %d %31s", &n, a1) == 2 && n >= 1 && n <= 32) {
            uint16_t v = 524;
            if (!strcmp(a1, "ouvert")) v = 677;
            else if (!strcmp(a1, "court")) v = 91;
            else if (!strcmp(a1, "coupe")) v = 1000;
            loops[n - 1] = v;
            advance(500);
        } else if (sscanf(line, "mes %31s", a1) == 1) {
            uint32_t open;
            int r = alarm_app_arm(&app, a1, ARM_AWAY, false, now_ms, &open);
            printf("  resultat=%d zones=0x%08X\n", r, (unsigned)open);
        } else if (sscanf(line, "mesp %31s", a1) == 1) {
            printf("  resultat=%d\n", alarm_app_arm(&app, a1, ARM_STAY, false, now_ms, NULL));
        } else if (sscanf(line, "mhs %31s", a1) == 1) {
            alarm_app_disarm(&app, a1, ORIGIN_SCREEN, now_ms);
        } else if (!strncmp(line, "sms ", 4)) {
            char rep[200];
            alarm_app_remote(&app, line + 4, ORIGIN_SMS, now_ms, rep, sizeof rep);
            printf("  [REPONSE SMS] %s\n", rep);
        } else if (sscanf(line, "secteur %31s", a2) == 1) {
            pm.ac_ok = a2[0] == '1';
        } else if (sscanf(line, "att %d", &n) == 1) {
            advance((ms_t)n * 1000u);
        } else if (!strcmp(line, "etat")) {
            char s[300];
            status_summary(&cfg, &app.engine, now_ms, s, sizeof s);
            printf("  %s\n", s);
        } else if (!strcmp(line, "journal")) {
            alarm_event_t ev;
            for (uint32_t i = 0; event_log_get(&app.log, i, &ev) && i < 20; i++) {
                char s[160];
                event_format(&ev, &cfg, s, sizeof s);
                printf("  %s\n", s);
            }
        } else if (!strcmp(line, "quitter")) {
            break;
        } else if (line[0]) {
            puts("  z <n> repos|ouvert|court|coupe, mes|mesp|mhs <code>, sms <txt>,"
                 " secteur 0|1, att <s>, etat, journal, quitter");
        }
        advance(10);
        printf("> ");
    }
    return 0;
}
