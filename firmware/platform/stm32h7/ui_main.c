/**
 * @file ui_main.c
 * @brief Interface tactile 1024×768 (LVGL v9).
 *
 * Onglets : Accueil | Zones | Sorties | Journal | Système
 *  - Accueil : bandeau d'état coloré, compte à rebours, boutons
 *              « Marche totale », « Marche partielle », « Arrêt », « Panique ».
 *  - Zones   : grille 8 × 4 des 32 zones (couleur = état), appui = inhibition.
 *  - Sorties : mode électrique (sec / 12 V / 5 V), commande manuelle.
 *  - Journal : 100 derniers événements.
 *  - Système : tensions, batterie, GSM, réseau, défauts, réglages.
 * Toute action sensible ouvre le clavier à code.
 *
 * Polices : générer avec lv_font_conv les polices ui_font_20/28/48
 * (Montserrat, plage 0x20-0x7F + 0xA0-0xFF pour les accents français),
 * voir tools/gen_fonts.sh.
 */
#include "app_main.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(ui_font_20);
LV_FONT_DECLARE(ui_font_28);
LV_FONT_DECLARE(ui_font_48);

void bsp_backlight_set(uint8_t percent);
uint32_t bsp_rtc_get_unix(void);

/* Palette */
#define C_BG        lv_color_hex(0x10151C)
#define C_PANEL     lv_color_hex(0x1C2430)
#define C_TEXT      lv_color_hex(0xE8EDF2)
#define C_GREEN     lv_color_hex(0x2E9E5B)
#define C_RED       lv_color_hex(0xD23B3B)
#define C_ORANGE    lv_color_hex(0xE08A1E)
#define C_BLUE      lv_color_hex(0x2F6FD6)
#define C_GREY      lv_color_hex(0x5A6675)
#define C_PURPLE    lv_color_hex(0x8E44AD)

/* ---------------------------------------------------------------------- */
/* Clavier à code (fenêtre modale)                                        */
/* ---------------------------------------------------------------------- */
typedef void (*pin_action_t)(const char *pin, intptr_t arg);

static lv_obj_t    *pin_modal;
static lv_obj_t    *pin_ta;
static lv_obj_t    *pin_msg;
static pin_action_t pin_cb;
static intptr_t     pin_arg;

static void pin_close(void)
{
    if (pin_modal) { lv_obj_delete(pin_modal); pin_modal = NULL; }
}

static void pin_key_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target_obj(e);
    const char *t = lv_buttonmatrix_get_button_text(bm, lv_buttonmatrix_get_selected_button(bm));
    if (!t) return;
    if (!strcmp(t, "Annuler")) { pin_close(); return; }
    if (!strcmp(t, LV_SYMBOL_BACKSPACE)) { lv_textarea_delete_char(pin_ta); return; }
    if (!strcmp(t, "OK")) {
        char pin[12];
        strncpy(pin, lv_textarea_get_text(pin_ta), sizeof pin - 1);
        pin[sizeof pin - 1] = 0;
        pin_action_t cb = pin_cb;
        intptr_t arg = pin_arg;
        pin_close();
        if (cb) cb(pin, arg);
        memset(pin, 0, sizeof pin);
        return;
    }
    lv_textarea_add_text(pin_ta, t);
}

static void pin_ask(const char *title, pin_action_t cb, intptr_t arg)
{
    static const char *map[] = { "1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                 LV_SYMBOL_BACKSPACE, "0", "OK", "\n", "Annuler", "" };
    pin_close();
    pin_cb = cb;
    pin_arg = arg;
    pin_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(pin_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(pin_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pin_modal, LV_OPA_70, 0);

    lv_obj_t *box = lv_obj_create(pin_modal);
    lv_obj_set_size(box, 440, 640);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, C_PANEL, 0);

    lv_obj_t *l = lv_label_create(box);
    lv_obj_set_style_text_font(l, &ui_font_28, 0);
    lv_label_set_text(l, title);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 0);

    pin_ta = lv_textarea_create(box);
    lv_textarea_set_one_line(pin_ta, true);
    lv_textarea_set_password_mode(pin_ta, true);
    lv_textarea_set_max_length(pin_ta, 8);
    lv_textarea_set_accepted_chars(pin_ta, "0123456789");
    lv_obj_set_style_text_font(pin_ta, &ui_font_48, 0);
    lv_obj_set_width(pin_ta, 380);
    lv_obj_align(pin_ta, LV_ALIGN_TOP_MID, 0, 45);

    pin_msg = lv_label_create(box);
    lv_label_set_text(pin_msg, "");
    lv_obj_align(pin_msg, LV_ALIGN_TOP_MID, 0, 125);

    lv_obj_t *bm = lv_buttonmatrix_create(box);
    lv_buttonmatrix_set_map(bm, map);
    lv_obj_set_size(bm, 400, 440);
    lv_obj_set_style_text_font(bm, &ui_font_28, 0);
    lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(bm, pin_key_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

/* Bandeau de message temporaire */
static void toast(const char *txt, lv_color_t col)
{
    lv_obj_t *t = lv_label_create(lv_layer_top());
    lv_label_set_text(t, txt);
    lv_obj_set_style_text_font(t, &ui_font_28, 0);
    lv_obj_set_style_bg_color(t, col, 0);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(t, 16, 0);
    lv_obj_set_style_radius(t, 8, 0);
    lv_obj_align(t, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_delete_delayed(t, 3500);
}

/* ---------------------------------------------------------------------- */
/* Accueil                                                                */
/* ---------------------------------------------------------------------- */
static lv_obj_t *home_status, *home_status_lbl, *home_delay_lbl, *home_info_lbl;
static lv_obj_t *top_clock, *top_icons;

enum { ACT_ARM_AWAY, ACT_ARM_STAY, ACT_DISARM, ACT_PANIC, ACT_BYPASS, ACT_OUTPUT_MODE,
       ACT_SETTINGS };

static void do_arm(const char *pin, intptr_t mode)
{
    uint32_t open = 0;
    core_lock();
    arm_result_t r = alarm_app_arm(&g_app, pin, (arm_mode_t)mode, false, app_now(), &open);
    core_unlock();
    char buf[160];
    switch (r) {
    case ARM_OK:        toast("Mise en service en cours", C_GREEN); break;
    case ARM_BAD_CODE:  toast("Code erroné", C_RED); break;
    case ARM_ALREADY:   toast("Déjà en service", C_ORANGE); break;
    default: {
        int n = snprintf(buf, sizeof buf, "%s :", r == ARM_TAMPER_PRESENT ? "Sabotage" : "Zones ouvertes");
        for (unsigned z = 0; z < NB_ZONES && n < (int)sizeof buf - 4; z++)
            if (open & (1u << z)) n += snprintf(buf + n, sizeof buf - (size_t)n, " %u", z + 1);
        toast(buf, C_RED);
    }
    }
}

static void do_disarm(const char *pin, intptr_t arg)
{
    (void)arg;
    core_lock();
    bool ok = alarm_app_disarm(&g_app, pin, ORIGIN_SCREEN, app_now());
    core_unlock();
    toast(ok ? "Système à l'arrêt" : "Code erroné", ok ? C_GREEN : C_RED);
}

static void home_btn_cb(lv_event_t *e)
{
    intptr_t act = (intptr_t)lv_event_get_user_data(e);
    switch (act) {
    case ACT_ARM_AWAY: pin_ask("Marche totale", do_arm, ARM_AWAY); break;
    case ACT_ARM_STAY: pin_ask("Marche partielle", do_arm, ARM_STAY); break;
    case ACT_DISARM:   pin_ask("Arrêt / acquittement", do_disarm, 0); break;
    case ACT_PANIC:
        core_lock();
        engine_panic(&g_app.engine, USER_NONE, ORIGIN_SCREEN, app_now());
        core_unlock();
        break;
    }
}

static lv_obj_t *big_button(lv_obj_t *parent, const char *txt, lv_color_t col, intptr_t act)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 230, 130);
    lv_obj_set_style_bg_color(b, col, 0);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &ui_font_28, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, home_btn_cb, act == ACT_PANIC ? LV_EVENT_LONG_PRESSED : LV_EVENT_CLICKED,
                        (void *)act);
    return b;
}

static void build_home(lv_obj_t *tab)
{
    lv_obj_set_style_pad_all(tab, 16, 0);
    home_status = lv_obj_create(tab);
    lv_obj_set_size(home_status, LV_PCT(100), 300);
    lv_obj_align(home_status, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(home_status, 16, 0);
    lv_obj_remove_flag(home_status, LV_OBJ_FLAG_SCROLLABLE);

    home_status_lbl = lv_label_create(home_status);
    lv_obj_set_style_text_font(home_status_lbl, &ui_font_48, 0);
    lv_obj_align(home_status_lbl, LV_ALIGN_CENTER, 0, -40);

    home_delay_lbl = lv_label_create(home_status);
    lv_obj_set_style_text_font(home_delay_lbl, &ui_font_48, 0);
    lv_obj_align(home_delay_lbl, LV_ALIGN_CENTER, 0, 40);

    home_info_lbl = lv_label_create(home_status);
    lv_obj_set_style_text_font(home_info_lbl, &ui_font_20, 0);
    lv_obj_set_width(home_info_lbl, LV_PCT(95));
    lv_label_set_long_mode(home_info_lbl, LV_LABEL_LONG_DOT);
    lv_obj_align(home_info_lbl, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t *row = lv_obj_create(tab);
    lv_obj_set_size(row, LV_PCT(100), 170);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    big_button(row, LV_SYMBOL_HOME "\nMarche\ntotale", C_RED, ACT_ARM_AWAY);
    big_button(row, LV_SYMBOL_EYE_OPEN "\nMarche\npartielle", C_ORANGE, ACT_ARM_STAY);
    big_button(row, LV_SYMBOL_POWER "\nArrêt", C_GREEN, ACT_DISARM);
    big_button(row, LV_SYMBOL_WARNING "\nPanique\n(appui long)", C_PURPLE, ACT_PANIC);
}

/* ---------------------------------------------------------------------- */
/* Zones                                                                  */
/* ---------------------------------------------------------------------- */
static lv_obj_t *zone_btn[NB_ZONES];

static void do_bypass(const char *pin, intptr_t z)
{
    int user;
    core_lock();
    auth_result_t r = alarm_app_screen_auth(&g_app, pin, app_now(), &user);
    bool ok = false;
    if (r == AUTH_OK && g_cfg.users[user].level >= LEVEL_MASTER) {
        bool cur = (g_app.engine.bypass >> z) & 1u;
        ok = engine_set_bypass(&g_app.engine, (unsigned)z, !cur, (uint8_t)user);
    }
    core_unlock();
    toast(ok ? "Inhibition modifiée" : "Refusé (code maître, système à l'arrêt)", ok ? C_BLUE : C_RED);
}

static void zone_cb(lv_event_t *e)
{
    intptr_t z = (intptr_t)lv_event_get_user_data(e);
    char t[48];
    snprintf(t, sizeof t, "Inhiber / rétablir Z%d", (int)z + 1);
    pin_ask(t, do_bypass, z);
}

static void build_zones(lv_obj_t *tab)
{
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_all(tab, 8, 0);
    lv_obj_set_style_pad_gap(tab, 8, 0);
    for (unsigned z = 0; z < NB_ZONES; z++) {
        lv_obj_t *b = lv_button_create(tab);
        lv_obj_set_size(b, 117, 140);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_width(l, 105);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, zone_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)z);
        zone_btn[z] = b;
    }
}

/* ---------------------------------------------------------------------- */
/* Sorties                                                                */
/* ---------------------------------------------------------------------- */
static lv_obj_t *out_dd[NB_OUTPUTS], *out_sw[NB_OUTPUTS], *out_led[NB_OUTPUTS];
static int pending_mode_out, pending_mode_val;

static void do_set_mode(const char *pin, intptr_t arg)
{
    (void)arg;
    int user;
    core_lock();
    bool ok = alarm_app_screen_auth(&g_app, pin, app_now(), &user) == AUTH_OK &&
              g_cfg.users[user].level >= LEVEL_INSTALLER;
    if (ok) {
        g_cfg.outputs[pending_mode_out].mode = (uint8_t)pending_mode_val;
        config_seal(&g_cfg);
    }
    core_unlock();
    if (ok) ok = app_save_config();
    toast(ok ? "Mode de sortie enregistré" : "Refusé (code installateur)", ok ? C_BLUE : C_RED);
}

static void out_mode_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    pending_mode_out = (int)(intptr_t)lv_event_get_user_data(e);
    pending_mode_val = (int)lv_dropdown_get_selected(dd);
    /* Le menu revient à la valeur réelle tant que le code n'est pas validé */
    lv_dropdown_set_selected(dd, g_cfg.outputs[pending_mode_out].mode);
    pin_ask("Changer le mode (installateur)", do_set_mode, 0);
}

static void out_sw_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    unsigned i = (unsigned)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    core_lock();
    outputs_set_remote(&g_app.outputs, i, on);
    core_unlock();
}

static void build_outputs(lv_obj_t *tab)
{
    static const char *const fn_txt[] = { "Aucune", "Sirène", "Sirène incendie", "Flash",
        "Report marche", "Report défaut", "Recopie zones", "Télécommande", "Buzzer tempo",
        "Report panique", "Report technique" };
    lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(tab, 10, 0);
    for (unsigned i = 0; i < NB_OUTPUTS; i++) {
        lv_obj_t *row = lv_obj_create(tab);
        lv_obj_set_size(row, LV_PCT(100), 88);
        lv_obj_set_style_bg_color(row, C_PANEL, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        out_led[i] = lv_led_create(row);
        lv_obj_set_size(out_led[i], 26, 26);
        lv_obj_align(out_led[i], LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *l = lv_label_create(row);
        lv_obj_set_style_text_font(l, &ui_font_28, 0);
        lv_label_set_text_fmt(l, "S%u  %s", i + 1, g_cfg.outputs[i].name);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 45, -14);
        lv_obj_t *f = lv_label_create(row);
        lv_label_set_text(f, fn_txt[g_cfg.outputs[i].function]);
        lv_obj_align(f, LV_ALIGN_LEFT_MID, 45, 20);

        out_dd[i] = lv_dropdown_create(row);
        lv_dropdown_set_options(out_dd[i], "Contact sec\n+12 V\n+5 V");
        lv_dropdown_set_selected(out_dd[i], g_cfg.outputs[i].mode);
        lv_obj_set_width(out_dd[i], 220);
        lv_obj_align(out_dd[i], LV_ALIGN_RIGHT_MID, -140, 0);
        lv_obj_add_event_cb(out_dd[i], out_mode_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);

        out_sw[i] = lv_switch_create(row);
        lv_obj_set_size(out_sw[i], 100, 50);
        lv_obj_align(out_sw[i], LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_event_cb(out_sw[i], out_sw_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
        if (g_cfg.outputs[i].function != OUT_FN_REMOTE) lv_obj_add_state(out_sw[i], LV_STATE_DISABLED);
    }
}

/* ---------------------------------------------------------------------- */
/* Journal                                                                */
/* ---------------------------------------------------------------------- */
static lv_obj_t *log_table;
static uint32_t  log_shown_head = 0xFFFFFFFFu;

static void build_journal(lv_obj_t *tab)
{
    log_table = lv_table_create(tab);
    lv_table_set_column_count(log_table, 1);
    lv_table_set_column_width(log_table, 0, 960);
    lv_obj_set_size(log_table, LV_PCT(100), LV_PCT(100));
}

static void refresh_journal(void)
{
    core_lock();
    uint32_t head = g_app.log.head;
    if (head == log_shown_head) { core_unlock(); return; }
    log_shown_head = head;
    uint32_t n = event_log_count(&g_app.log);
    if (n > 100) n = 100;
    lv_table_set_row_count(log_table, n);
    for (uint32_t i = 0; i < n; i++) {
        alarm_event_t ev;
        char line[160];
        event_log_get(&g_app.log, i, &ev);
        event_format(&ev, &g_cfg, line, sizeof line);
        lv_table_set_cell_value(log_table, i, 0, line);
    }
    core_unlock();
}

/* ---------------------------------------------------------------------- */
/* Système                                                                */
/* ---------------------------------------------------------------------- */
static lv_obj_t *sys_lbl, *sys_vol, *sys_exit, *sys_entry;

static void do_settings(const char *pin, intptr_t arg)
{
    (void)arg;
    int user;
    core_lock();
    bool ok = alarm_app_screen_auth(&g_app, pin, app_now(), &user) == AUTH_OK &&
              g_cfg.users[user].level >= LEVEL_MASTER;
    if (ok) {
        g_cfg.volume       = (uint8_t)lv_slider_get_value(sys_vol);
        g_cfg.exit_delay_s = (uint16_t)lv_slider_get_value(sys_exit);
        g_cfg.entry_delay_s = (uint16_t)lv_slider_get_value(sys_entry);
        config_seal(&g_cfg);
    }
    core_unlock();
    if (ok) ok = app_save_config();
    toast(ok ? "Réglages enregistrés" : "Refusé", ok ? C_BLUE : C_RED);
}

static void save_cb(lv_event_t *e) { (void)e; pin_ask("Enregistrer les réglages", do_settings, 0); }

static lv_obj_t *labeled_slider(lv_obj_t *p, const char *txt, int min, int max, int val, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -40, y);
    lv_obj_t *s = lv_slider_create(p);
    lv_slider_set_range(s, min, max);
    lv_slider_set_value(s, val, LV_ANIM_OFF);
    lv_obj_set_width(s, 380);
    lv_obj_align(s, LV_ALIGN_TOP_RIGHT, -40, y + 36);
    return s;
}

static void build_system(lv_obj_t *tab)
{
    sys_lbl = lv_label_create(tab);
    lv_obj_set_style_text_font(sys_lbl, &ui_font_20, 0);
    lv_obj_align(sys_lbl, LV_ALIGN_TOP_LEFT, 10, 10);

    sys_vol   = labeled_slider(tab, "Volume des annonces (%)", 0, 100, g_cfg.volume, 10);
    sys_exit  = labeled_slider(tab, "Temporisation de sortie (s)", 0, 120, g_cfg.exit_delay_s, 110);
    sys_entry = labeled_slider(tab, "Temporisation d'entrée (s)", 0, 60, g_cfg.entry_delay_s, 210);

    lv_obj_t *b = lv_button_create(tab);
    lv_obj_set_size(b, 380, 80);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, -40, 320);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_SAVE "  Enregistrer");
    lv_obj_center(l);
    lv_obj_add_event_cb(b, save_cb, LV_EVENT_CLICKED, NULL);
}

/* ---------------------------------------------------------------------- */
/* Rafraîchissement périodique                                            */
/* ---------------------------------------------------------------------- */
static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    char info[256];
    core_lock();
    const alarm_engine_t *e = &g_app.engine;
    ms_t now = app_now();
    uint32_t rem = engine_delay_remaining_s(e, now);
    const char *st = engine_state_label(e);
    lv_color_t col = C_GREEN;
    if (e->state != SYS_DISARMED) col = C_BLUE;
    if (rem) col = C_ORANGE;
    if (e->active_alarms & (ALM_INTRUSION | ALM_FIRE | ALM_TAMPER))
        col = ((now / 500u) & 1u) ? C_RED : lv_color_hex(0x7A1010);
    status_summary(&g_cfg, e, now, info, sizeof info);

    zone_state_t zs[NB_ZONES];
    memcpy(zs, e->zone, sizeof zs);
    uint32_t byp = e->bypass | e->auto_bypass, mem = e->alarm_memory;
    bool outs[NB_OUTPUTS];
    for (unsigned i = 0; i < NB_OUTPUTS; i++) outs[i] = g_app.outputs.ch[i].main_on;
    power_meas_t p = g_app.last_power;
    uint32_t faults = e->faults;
    core_unlock();

    lv_obj_set_style_bg_color(home_status, col, 0);
    lv_label_set_text(home_status_lbl, st);
    if (rem) lv_label_set_text_fmt(home_delay_lbl, "%lu s", (unsigned long)rem);
    else     lv_label_set_text(home_delay_lbl, "");
    lv_label_set_text(home_info_lbl, info);

    for (unsigned z = 0; z < NB_ZONES; z++) {
        lv_color_t c = C_GREEN;
        if (g_cfg.zones[z].type == ZT_DISABLED) c = C_GREY;
        else if (byp & (1u << z)) c = C_BLUE;
        else if (zs[z] == ZSTATE_ACTIVE) c = C_ORANGE;
        else if (zs[z] == ZSTATE_TAMPER || zs[z] == ZSTATE_FAULT) c = C_PURPLE;
        if (mem & (1u << z)) c = C_RED;
        lv_obj_set_style_bg_color(zone_btn[z], c, 0);
        lv_label_set_text_fmt(lv_obj_get_child(zone_btn[z], 0), "%u\n%s", z + 1, g_cfg.zones[z].name);
    }
    for (unsigned i = 0; i < NB_OUTPUTS; i++) {
        if (outs[i]) lv_led_on(out_led[i]); else lv_led_off(out_led[i]);
    }

    int n = snprintf(info, sizeof info,
                     "Secteur : %s\nBatterie : %u,%02u V  %+d mA\nEntrée : %u,%01u V   5 V : %u,%02u V\n"
                     "Aux 12 V : %u,%01u V\nGSM : %s %d dBm\nEthernet : %s %u.%u.%u.%u\nDéfauts :",
                     p.ac_ok ? "présent" : "ABSENT", p.vbat_mv / 1000u, (p.vbat_mv % 1000u) / 10u,
                     p.ibat_ma, p.vin_mv / 1000u, (p.vin_mv % 1000u) / 100u, p.v5_mv / 1000u,
                     (p.v5_mv % 1000u) / 10u, p.vaux_mv / 1000u, (p.vaux_mv % 1000u) / 100u,
                     g_comm.registered ? "enregistré" : "hors réseau", g_comm.rssi_dbm,
                     g_comm.eth_link ? "connecté" : "déconnecté",
                     g_comm.ip[0], g_comm.ip[1], g_comm.ip[2], g_comm.ip[3]);
    if (!faults) snprintf(info + n, sizeof info - (size_t)n, " aucun");
    for (unsigned f = 0; f < FAULT_COUNT && n < (int)sizeof info - 30; f++)
        if (faults & (1u << f)) n += snprintf(info + n, sizeof info - (size_t)n, "\n - %s", fault_label((fault_t)f));
    lv_label_set_text(sys_lbl, info);

    refresh_journal();

    /* Horloge et icônes de la barre supérieure */
    uint32_t ut = bsp_rtc_get_unix() + (uint32_t)(g_cfg.utc_offset_min * 60);
    lv_label_set_text_fmt(top_clock, "%02lu:%02lu", (unsigned long)((ut / 3600u) % 24u),
                          (unsigned long)((ut / 60u) % 60u));
    lv_label_set_text_fmt(top_icons, "%s %s %s",
                          p.ac_ok ? LV_SYMBOL_CHARGE : LV_SYMBOL_BATTERY_2,
                          g_comm.eth_link ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE,
                          g_comm.registered ? LV_SYMBOL_CALL : LV_SYMBOL_WARNING);
}

void ui_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_text_color(scr, C_TEXT, 0);
    lv_obj_set_style_text_font(scr, &ui_font_20, 0);

    lv_obj_t *tv = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_size(tv, 70);
    lv_obj_set_style_bg_color(tv, C_BG, 0);
    build_home(lv_tabview_add_tab(tv, "Accueil"));
    build_zones(lv_tabview_add_tab(tv, "Zones"));
    build_outputs(lv_tabview_add_tab(tv, "Sorties"));
    build_journal(lv_tabview_add_tab(tv, "Journal"));
    build_system(lv_tabview_add_tab(tv, "Système"));

    top_clock = lv_label_create(lv_layer_top());
    lv_obj_set_style_text_font(top_clock, &ui_font_28, 0);
    lv_obj_align(top_clock, LV_ALIGN_TOP_RIGHT, -16, 18);
    top_icons = lv_label_create(lv_layer_top());
    lv_obj_align(top_icons, LV_ALIGN_TOP_RIGHT, -120, 22);

    lv_timer_create(refresh_cb, 250, NULL);
}

/** Tâche IHM : seule tâche autorisée à appeler LVGL. */
void ui_task(void *arg)
{
    (void)arg;
    extern void bsp_display_init(void);
    bsp_display_init();
    ui_init();
    for (;;) {
        uint32_t d = lv_timer_handler();
        if (d > 20u) d = 20u;
        vTaskDelay(pdMS_TO_TICKS(d ? d : 1));
    }
}
