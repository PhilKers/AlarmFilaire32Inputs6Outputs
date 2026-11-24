/**
 * @file test_core.c
 * @brief Tests unitaires du noyau (exécutés sur PC : ctest ou ./test_core).
 */
#include "alarm_app.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_fail, g_pass;
#define CHECK(c) do { if (c) g_pass++; else { g_fail++; \
    printf("  ECHEC %s:%d : %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- Bouchons plateforme --------------------------------------------- */
static alarm_event_t g_ev[256];
static unsigned g_nev;
static uint16_t g_voice[16];
static unsigned g_nvoice;

static uint32_t fake_time(void) { return 1795000000u; }
static void fake_voice(const uint16_t *s, unsigned n)
{
    g_nvoice = n; memcpy(g_voice, s, n * sizeof *s);
}
static void fake_persist(const alarm_event_t *ev) { if (g_nev < 256) g_ev[g_nev++] = *ev; }
static const app_hooks_t hooks = { fake_time, fake_voice, fake_persist, NULL };

static bool has_event(event_type_t t, uint8_t src)
{
    for (unsigned i = 0; i < g_nev; i++)
        if (g_ev[i].type == t && (src == SRC_NONE || g_ev[i].source == src)) return true;
    return false;
}

/* Mesures « boucle » pour EOL 4k7 / pull-up 4k7 / série 470 Ω */
#define P_SHORT  91
#define P_REST   524
#define P_ALARM  677
#define P_OPEN   1000

static uint16_t loops[NB_ZONES];
static void set_all(uint16_t v) { for (unsigned i = 0; i < NB_ZONES; i++) loops[i] = v; }

/* Fait avancer le temps par pas de 10 ms */
static ms_t run(alarm_app_t *a, ms_t t, ms_t dur)
{
    for (ms_t end = t + dur; t < end; t += 10) {
        alarm_app_zones(a, loops, t);
        alarm_app_tick(a, t);
    }
    return t;
}

static void setup(alarm_app_t *a, alarm_config_t *cfg)
{
    config_defaults(cfg);
    for (unsigned z = 0; z < 6; z++) cfg->zones[z].type = ZT_INSTANT;
    cfg->zones[0].type = ZT_DELAYED;
    cfg->zones[1].type = ZT_FOLLOWER;
    cfg->zones[2].flags |= ZF_PARTIAL;
    cfg->zones[10].type = ZT_FIRE;
    cfg->zones[11].type = ZT_PANIC;
    cfg->zones[11].flags |= ZF_SILENT;
    cfg->zones[12].type = ZT_TECHNICAL;
    cfg->exit_delay_s = 10;
    cfg->entry_delay_s = 5;
    cfg->siren_duration_s = 20;
    config_seal(cfg);
    g_nev = 0; g_nvoice = 0;
    set_all(P_REST);
    alarm_app_init(a, cfg, &hooks);
}

/* ---------------------------------------------------------------------- */
static void test_sha256(void)
{
    puts("sha256");
    uint8_t h[32];
    sha256((const uint8_t *)"abc", 3, h);
    static const uint8_t ref[4] = { 0xba, 0x78, 0x16, 0xbf };
    CHECK(memcmp(h, ref, 4) == 0);
    CHECK(h[31] == 0xad);
}

static void test_config(void)
{
    puts("configuration");
    static alarm_config_t c;
    config_defaults(&c);
    CHECK(config_is_valid(&c));
    c.exit_delay_s++;
    CHECK(!config_is_valid(&c));
    config_seal(&c);
    CHECK(config_is_valid(&c));
    CHECK(crc32_compute("123456789", 9) == 0xCBF43926u);
}

static void test_classify(void)
{
    puts("classification des boucles");
    loop_thresholds_t t = { 300, 600, 850, 0 };
    CHECK(loop_classify(P_SHORT, &t) == LOOP_SHORT);
    CHECK(loop_classify(P_REST, &t) == LOOP_NORMAL);
    CHECK(loop_classify(P_ALARM, &t) == LOOP_ALARM);
    CHECK(loop_classify(P_OPEN, &t) == LOOP_OPEN);
    CHECK(loop_classify(1500, &t) == LOOP_INVALID);

    CHECK(zone_interpret(LOOP_NORMAL, WIRING_DEOL) == ZSTATE_REST);
    CHECK(zone_interpret(LOOP_ALARM, WIRING_DEOL) == ZSTATE_ACTIVE);
    CHECK(zone_interpret(LOOP_SHORT, WIRING_DEOL) == ZSTATE_TAMPER);
    CHECK(zone_interpret(LOOP_OPEN, WIRING_DEOL) == ZSTATE_TAMPER);
    CHECK(zone_interpret(LOOP_OPEN, WIRING_SEOL) == ZSTATE_ACTIVE);
    CHECK(zone_interpret(LOOP_SHORT, WIRING_SEOL) == ZSTATE_TAMPER);
    CHECK(zone_interpret(LOOP_SHORT, WIRING_NC) == ZSTATE_REST);
    CHECK(zone_interpret(LOOP_OPEN, WIRING_NC) == ZSTATE_ACTIVE);
    CHECK(zone_interpret(LOOP_SHORT, WIRING_NO) == ZSTATE_ACTIVE);
    CHECK(zone_interpret(LOOP_OPEN, WIRING_NO) == ZSTATE_REST);
}

static void test_debounce(void)
{
    puts("anti-rebond");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    ms_t t = run(&a, 0, 100);
    loops[3] = P_ALARM;
    t = run(&a, t, 200);                       /* < 300 ms : pas encore */
    CHECK(a.engine.zone[3] == ZSTATE_REST);
    loops[3] = P_REST;                          /* rebond */
    t = run(&a, t, 50);
    loops[3] = P_ALARM;
    t = run(&a, t, 250);
    CHECK(a.engine.zone[3] == ZSTATE_REST);
    t = run(&a, t, 100);
    CHECK(a.engine.zone[3] == ZSTATE_ACTIVE);
    (void)t;
}

static void test_arm_exit_entry(void)
{
    puts("MES, tempo sortie, tempo entree, MHS");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    ms_t t = run(&a, 0, 500);

    CHECK(alarm_app_arm(&a, "9999", ARM_AWAY, false, t, NULL) == ARM_BAD_CODE);
    CHECK(has_event(EV_BAD_CODE, SRC_NONE));
    CHECK(alarm_app_arm(&a, "1234", ARM_AWAY, false, t, NULL) == ARM_OK);
    CHECK(a.engine.state == SYS_EXIT_DELAY);

    /* Sortie par la porte (temporisée) puis le couloir (suiveuse) */
    loops[0] = P_ALARM; loops[1] = P_ALARM;
    t = run(&a, t, 2000);
    loops[0] = P_REST; loops[1] = P_REST;
    t = run(&a, t, 9000);
    CHECK(a.engine.state == SYS_ARMED);
    CHECK(!a.engine.siren_running);
    CHECK(has_event(EV_ARMED_AWAY, SRC_NONE));

    /* Retour : porte → tempo d'entrée, couloir ignoré */
    loops[0] = P_ALARM;
    t = run(&a, t, 400);
    CHECK(a.engine.state == SYS_ENTRY_DELAY);
    loops[1] = P_ALARM;
    t = run(&a, t, 1000);
    CHECK(!a.engine.siren_running);
    CHECK(alarm_app_disarm(&a, "1234", ORIGIN_SCREEN, t));
    CHECK(a.engine.state == SYS_DISARMED);
    CHECK(!a.engine.siren_running);
    (void)t;
}

static void test_entry_timeout_and_siren(void)
{
    puts("tempo d'entree ecoulee, sirene, sortie relais");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    cfg.exit_delay_s = 0; config_seal(&cfg);
    ms_t t = run(&a, 0, 500);
    CHECK(alarm_app_arm(&a, "1234", ARM_AWAY, false, t, NULL) == ARM_OK);
    CHECK(a.engine.state == SYS_ARMED);
    loops[0] = P_ALARM;
    t = run(&a, t, 400 + 5000 + 100);
    CHECK(a.engine.siren_running);
    CHECK(has_event(EV_ALARM_INTRUSION, 0));
    CHECK(g_nvoice >= 1);

    /* Sorties 1 et 2 = sirènes en mode 12 V : séquence de mode terminée */
    uint32_t w = alarm_app_tick(&a, t);
    CHECK(w & RLY_MAIN(0));
    CHECK(w & RLY_SRC(0));
    CHECK(!(w & RLY_VSEL(0)));
    CHECK(w & RLY_MAIN(2));                 /* flash */
    CHECK(w & RLY_MAIN(3));                 /* report état armé */

    t = run(&a, t, 21000);                   /* durée sirène 20 s */
    CHECK(!a.engine.siren_running);
    CHECK(has_event(EV_SIREN_TIMEOUT, SRC_NONE));
    w = alarm_app_tick(&a, t);
    CHECK(!(w & RLY_MAIN(0)));
    CHECK(w & RLY_MAIN(2));                 /* flash maintenu jusqu'à MHS */
    alarm_app_disarm(&a, "1234", ORIGIN_SCREEN, t);
    w = alarm_app_tick(&a, t);
    CHECK(!(w & RLY_MAIN(2)));
}

static void test_arm_refused_and_force(void)
{
    puts("MES refusee (zone ouverte), MES forcee");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    loops[4] = P_ALARM;
    ms_t t = run(&a, 0, 500);
    uint32_t open = 0;
    CHECK(alarm_app_arm(&a, "1234", ARM_AWAY, false, t, &open) == ARM_ZONES_OPEN);
    CHECK(open == (1u << 4));
    CHECK(alarm_app_arm(&a, "1234", ARM_AWAY, true, t, &open) == ARM_OK);
    CHECK(a.engine.bypass & (1u << 4));
    /* Partielle : zone 4 non incluse → aurait été acceptée */
    alarm_app_disarm(&a, "1234", ORIGIN_SCREEN, t);
    CHECK(alarm_app_arm(&a, "1234", ARM_STAY, false, t, &open) == ARM_OK);
    (void)t;
}

static void test_tamper(void)
{
    puts("sabotage DEOL");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    ms_t t = run(&a, 0, 500);
    loops[5] = P_SHORT;
    t = run(&a, t, 200);
    CHECK(a.engine.zone[5] == ZSTATE_TAMPER);
    CHECK(a.engine.active_alarms & ALM_TAMPER);
    CHECK(!a.engine.siren_running);         /* désarmé : pas de sirène par défaut */
    CHECK(alarm_app_arm(&a, "1234", ARM_AWAY, false, t, NULL) == ARM_TAMPER_PRESENT);
    (void)t;
}

static void test_fire_and_panic(void)
{
    puts("incendie 24h, panique silencieuse, technique");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    ms_t t = run(&a, 0, 500);
    loops[11] = P_ALARM;
    t = run(&a, t, 400);
    CHECK(a.engine.active_alarms & ALM_PANIC);
    CHECK(!a.engine.siren_running);
    CHECK(g_nvoice == 0 || g_voice[0] != VM_PANIC_CALL);

    loops[12] = P_ALARM;
    t = run(&a, t, 400);
    CHECK(a.engine.active_alarms & ALM_TECHNICAL);
    CHECK(!a.engine.siren_running);

    loops[10] = P_ALARM;
    t = run(&a, t, 400);
    CHECK(a.engine.active_alarms & ALM_FIRE);
    CHECK(a.engine.siren_running);
    /* Sirène incendie modulée : alternance sur 2 s */
    uint32_t on = 0;
    for (int i = 0; i < 200; i++) { if (alarm_app_tick(&a, t) & RLY_MAIN(0)) on++; t += 10; }
    CHECK(on > 60 && on < 140);
}

static void test_swinger(void)
{
    puts("zone bavarde (inhibition automatique)");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    cfg.exit_delay_s = 0; cfg.siren_duration_s = 1; config_seal(&cfg);
    ms_t t = run(&a, 0, 500);
    alarm_app_arm(&a, "1234", ARM_AWAY, false, t, NULL);
    for (int i = 0; i < 4; i++) {
        loops[3] = P_ALARM; t = run(&a, t, 500);
        loops[3] = P_REST;  t = run(&a, t, 1500);
    }
    CHECK(a.engine.alarm_count[3] == 3);
    CHECK(a.engine.auto_bypass & (1u << 3));
    CHECK(has_event(EV_AUTO_BYPASS, 3));
}

static void test_mode_switch(void)
{
    puts("changement de mode de sortie (break-before-make)");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    cfg.outputs[5].function = OUT_FN_REMOTE;
    cfg.outputs[5].mode = OUT_MODE_12V;
    config_seal(&cfg);
    outputs_init(&a.outputs, &cfg);
    ms_t t = run(&a, 0, 500);
    outputs_set_remote(&a.outputs, 5, true);
    uint32_t w = alarm_app_tick(&a, t);
    CHECK((w & RLY_MAIN(5)) && (w & RLY_SRC(5)) && !(w & RLY_VSEL(5)));

    cfg.outputs[5].mode = OUT_MODE_5V; config_seal(&cfg);
    bool seen_src_off_vsel_change = false, overlap = false;
    uint32_t prev = w;
    for (int i = 0; i < 40; i++) {
        t += 10;
        w = alarm_app_tick(&a, t);
        if ((w & RLY_MAIN(5)) && a.outputs.ch[5].stage != SW_IDLE) overlap = true;
        if (((w ^ prev) & RLY_VSEL(5)) && (w & RLY_SRC(5))) overlap = true;
        if (((w ^ prev) & RLY_VSEL(5)) && !(w & RLY_SRC(5))) seen_src_off_vsel_change = true;
        prev = w;
    }
    CHECK(!overlap);
    CHECK(seen_src_off_vsel_change);
    CHECK((w & RLY_MAIN(5)) && (w & RLY_SRC(5)) && (w & RLY_VSEL(5)));
}

static void test_remote_sms(void)
{
    puts("commandes SMS");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    cfg.outputs[5].function = OUT_FN_REMOTE; config_seal(&cfg);
    ms_t t = run(&a, 0, 500);
    char rep[200];
    CHECK(alarm_app_remote(&a, "1234 etat", ORIGIN_SMS, t, rep, sizeof rep));
    CHECK(strstr(rep, "Desarme") != NULL);
    CHECK(alarm_app_remote(&a, "1234 MES", ORIGIN_SMS, t, rep, sizeof rep));
    CHECK(a.engine.state == SYS_EXIT_DELAY);
    CHECK(alarm_app_remote(&a, "1234 mhs", ORIGIN_SMS, t, rep, sizeof rep));
    CHECK(a.engine.state == SYS_DISARMED);
    CHECK(alarm_app_remote(&a, "1234 SORTIE 6 ON", ORIGIN_SMS, t, rep, sizeof rep));
    CHECK(a.outputs.ch[5].remote);
    CHECK(!alarm_app_remote(&a, "1234 SORTIE 1 ON", ORIGIN_SMS, t, rep, sizeof rep));
    CHECK(!alarm_app_remote(&a, "bonjour", ORIGIN_SMS, t, rep, sizeof rep));
    for (int i = 0; i < 5; i++) alarm_app_remote(&a, "1111 ETAT", ORIGIN_SMS, t, rep, sizeof rep);
    CHECK(!alarm_app_remote(&a, "1234 ETAT", ORIGIN_SMS, t, rep, sizeof rep));
    CHECK(strstr(rep, "verrouille") != NULL);
    CHECK(alarm_app_remote(&a, "1234 ETAT", ORIGIN_SMS, t + AUTH_LOCKOUT_MS, rep, sizeof rep));
}

static void test_notifier(void)
{
    puts("notifications SMS + appels en cascade");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    strcpy(cfg.contacts[0].phone, "+33600000001");
    cfg.contacts[0].sms = 1; cfg.contacts[0].call = 1; cfg.contacts[0].event_mask = 0xFFFF;
    strcpy(cfg.contacts[2].phone, "+33600000003");
    cfg.contacts[2].call = 1; cfg.contacts[2].event_mask = NOTIF_INTRUSION;
    config_seal(&cfg);
    notifier_t n; notifier_init(&n);
    alarm_event_t ev = { 0 }; ev.type = EV_ALARM_INTRUSION; ev.source = 3;
    CHECK(notifier_on_event(&n, &cfg, &ev) == 2);   /* 1 SMS + 1 appel cascade */
    const notif_job_t *j = notifier_peek(&n);
    CHECK(j && j->kind == JOB_SMS);
    notifier_done(&n, &cfg, true);
    j = notifier_peek(&n);
    CHECK(j && j->kind == JOB_CALL && j->contact == 0);
    notifier_done(&n, &cfg, false);                  /* pas d'acquittement */
    j = notifier_peek(&n);
    CHECK(j && j->kind == JOB_CALL && j->contact == 2);
    notifier_done(&n, &cfg, true);                   /* acquitté */
    CHECK(notifier_peek(&n) == NULL);
    ev.type = EV_FAULT_ON; ev.source = FAULT_GSM;
    CHECK(notifier_on_event(&n, &cfg, &ev) == 0);
}

static void test_power(void)
{
    puts("surveillance alimentation");
    static alarm_config_t cfg; static alarm_app_t a;
    setup(&a, &cfg);
    power_meas_t m = { true, 13600, 13500, 200, 5000, 13400, 13400, 3300 };
    alarm_app_power(&a, &m, 1000);
    CHECK(a.engine.faults == 0);
    m.ac_ok = false;
    alarm_app_power(&a, &m, 2000);
    CHECK(!(a.engine.faults & (1u << FAULT_MAINS)));
    alarm_app_power(&a, &m, 62000);
    CHECK(a.engine.faults & (1u << FAULT_MAINS));
    m.vbat_mv = 11200;
    alarm_app_power(&a, &m, 63000);
    alarm_app_power(&a, &m, 74000);
    CHECK(a.engine.faults & (1u << FAULT_BATTERY_LOW));
    m.vbat_mv = 11900;                               /* hystérésis */
    alarm_app_power(&a, &m, 75000);
    CHECK(a.engine.faults & (1u << FAULT_BATTERY_LOW));
    m.vbat_mv = 12500; m.ac_ok = true;
    alarm_app_power(&a, &m, 76000);
    CHECK(a.engine.faults == 0);
    m.vaux_mv = 300;
    alarm_app_power(&a, &m, 77000);
    CHECK(a.engine.faults & (1u << FAULT_AUX_FUSE));
}

static void test_log_format(void)
{
    puts("journal");
    static alarm_config_t cfg; config_defaults(&cfg);
    event_log_t *l = malloc(sizeof *l);
    event_log_init(l);
    for (unsigned i = 0; i < EVENT_LOG_SIZE + 10; i++) {
        alarm_event_t ev = { 0 }; ev.type = EV_ZONE_OPEN; ev.source = (uint8_t)(i % 32);
        ev.time = i; event_log_push(l, &ev);
    }
    CHECK(event_log_count(l) == EVENT_LOG_SIZE);
    alarm_event_t e;
    CHECK(event_log_get(l, 0, &e) && e.time == EVENT_LOG_SIZE + 9);
    char s[120];
    alarm_event_t ev = { 1795000000u, EV_ALARM_INTRUSION, 0, USER_NONE, 0, {0} };
    event_format(&ev, &cfg, s, sizeof s);
    CHECK(strstr(s, "ALARME INTRUSION - Z1 Porte d'entree") != NULL);
    CHECK(strncmp(s, "2026-11-18", 10) == 0);
    free(l);
}

int main(void)
{
    test_sha256();
    test_config();
    test_classify();
    test_debounce();
    test_arm_exit_entry();
    test_entry_timeout_and_siren();
    test_arm_refused_and_force();
    test_tamper();
    test_fire_and_panic();
    test_swinger();
    test_mode_switch();
    test_remote_sms();
    test_notifier();
    test_power();
    test_log_format();
    printf("\n%d verifications reussies, %d echecs\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
