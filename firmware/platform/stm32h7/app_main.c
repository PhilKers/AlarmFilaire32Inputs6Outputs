/**
 * @file app_main.c
 * @brief Démarrage de l'application et tâches FreeRTOS.
 *
 * main.c (généré par CubeMX) initialise horloges, MPU, caches, SDRAM (FMC),
 * LTDC, ADC, SPI, I2C, USART, SAI, SDMMC, QUADSPI, RTC, RNG, LwIP, FatFS,
 * puis appelle app_start() avant vTaskStartScheduler().
 *
 *  Tâche        Prio  Période   Rôle
 *  ----------   ----  --------  ------------------------------------------
 *  alarm         6    10 ms     boucles, moteur, relais, autoprotection
 *  power         4    1 s       INA226, rails, défauts alimentation
 *  audio         4    évén.     annonces vocales SAI/DMA
 *  modem         3    évén.     SMS, appels, DTMF, réseau mobile
 *  net           3    évén.     HTTP/JSON, SNTP
 *  ui            2    5-20 ms   LVGL + tactile
 *  supervisor    7    500 ms    chien de garde (contrôle de vie des tâches)
 */
#include "app_main.h"
#include "bsp.h"
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "lwip/netif.h"
#include <string.h>
#include <stdio.h>

extern RTC_HandleTypeDef  hrtc;
extern RNG_HandleTypeDef  hrng;
extern IWDG_HandleTypeDef hiwdg1;
extern struct netif       gnetif;

void modem_task(void *arg);
void net_task(void *arg);
void ui_task(void *arg);

alarm_config_t g_cfg;
alarm_app_t    g_app;
volatile comm_status_t g_comm;
volatile uint16_t g_vzone_mv;

static SemaphoreHandle_t core_mtx;
static SemaphoreHandle_t save_mtx;

/* Contrôle de vie : chaque tâche critique positionne son bit à chaque cycle */
#define ALIVE_ALARM  (1u << 0)
#define ALIVE_POWER  (1u << 1)
#define ALIVE_ALL    (ALIVE_ALARM | ALIVE_POWER)
static volatile uint32_t alive_bits;

void core_lock(void)   { xSemaphoreTakeRecursive(core_mtx, portMAX_DELAY); }
void core_unlock(void) { xSemaphoreGiveRecursive(core_mtx); }
ms_t app_now(void)     { return (ms_t)xTaskGetTickCount(); }   /* configTICK_RATE_HZ = 1000 */

/* ---- Heure (RTC sur pile CR2032, base UNIX en UTC) -------------------- */
uint32_t bsp_rtc_get_unix(void)
{
    RTC_TimeTypeDef t; RTC_DateTypeDef d;
    HAL_RTC_GetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);      /* lecture obligatoire après GetTime */
    int y = 2000 + d.Year, m = d.Month, dd = d.Date;
    /* Jours depuis 1970 (algorithme inverse de H. Hinnant) */
    y -= m <= 2;
    int era = y / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int32_t days = era * 146097 + (int32_t)doe - 719468;
    return (uint32_t)days * 86400u + t.Hours * 3600u + t.Minutes * 60u + t.Seconds;
}

void bsp_rtc_set_unix(uint32_t ts)
{
    uint32_t days = ts / 86400u, rem = ts % 86400u;
    int32_t z = (int32_t)days + 719468, era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    uint32_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    int32_t y = (int32_t)yoe + era * 400 + (m <= 2);

    RTC_TimeTypeDef t = { 0 }; RTC_DateTypeDef dt = { 0 };
    t.Hours = (uint8_t)(rem / 3600u); t.Minutes = (uint8_t)((rem / 60u) % 60u); t.Seconds = (uint8_t)(rem % 60u);
    dt.Year = (uint8_t)(y - 2000); dt.Month = (uint8_t)m; dt.Date = (uint8_t)d;
    dt.WeekDay = (uint8_t)(((days + 3u) % 7u) + 1u);   /* 1970-01-01 = jeudi ; 1 = lundi */
    HAL_RTC_SetTime(&hrtc, &t, RTC_FORMAT_BIN);
    HAL_RTC_SetDate(&hrtc, &dt, RTC_FORMAT_BIN);
}

void bsp_random(uint8_t *buf, unsigned n)
{
    for (unsigned i = 0; i < n; i += 4) {
        uint32_t r = 0;
        HAL_RNG_GenerateRandomNumber(&hrng, &r);
        for (unsigned k = 0; k < 4 && i + k < n; k++) buf[i + k] = (uint8_t)(r >> (8 * k));
    }
}

/* ---- Crochets du noyau ------------------------------------------------- */
static void hook_voice(const uint16_t *seq, unsigned n) { bsp_audio_enqueue(seq, n); }

static QueueHandle_t persist_q;

/* Appelé sous core_lock() : simple copie dans une file ; l'écriture sur la
 * carte SD est faite par la tâche power (moins prioritaire que alarm). */
static void hook_persist(const alarm_event_t *ev) { if (persist_q) xQueueSend(persist_q, ev, 0); }

bool app_save_config(void)
{
    xSemaphoreTake(save_mtx, portMAX_DELAY);
    static alarm_config_t copy;
    core_lock();
    memcpy(&copy, &g_cfg, sizeof copy);
    alarm_event_t ev = { 0 };
    ev.type = EV_CONFIG_CHANGED; ev.source = SRC_NONE; ev.user = USER_NONE;
    if (g_app.engine.cb) g_app.engine.cb(g_app.engine.cb_ctx, &ev);
    core_unlock();
    bool ok = storage_save_config(&copy, sizeof copy);
    xSemaphoreGive(save_mtx);
    return ok;
}

/* ---- Tâches ------------------------------------------------------------ */
static void alarm_task(void *arg)
{
    (void)arg;
    static uint16_t permille[NB_ZONES];
    TickType_t last = xTaskGetTickCount();
    uint32_t prev_word = 0xFFFFFFFFu;
    unsigned chain_err = 0;
    bool tamper_prev = false;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
        uint16_t vz;
        bool ref_ok = bsp_zones_scan(permille, &vz);
        g_vzone_mv = vz;
        bool tamper = bsp_box_tamper_open();

        core_lock();
        ms_t now = app_now();
        alarm_app_zones(&g_app, ref_ok ? permille : NULL, now);
        if (tamper != tamper_prev) engine_set_box_tamper(&g_app.engine, tamper, now);
        uint32_t w = alarm_app_tick(&g_app, now);
        core_unlock();
        tamper_prev = tamper;

        /* Rafraîchit la chaîne à chaque changement et toutes les 100 ms
         * (corrige une éventuelle perturbation électromagnétique) */
        static unsigned refresh;
        if (w != prev_word || ++refresh >= 10) {
            refresh = 0;
            bool ok = bsp_relays_write(w);
            chain_err = ok ? 0 : chain_err + 1;
            prev_word = w;
            core_lock();
            engine_set_fault(&g_app.engine, FAULT_RELAY_CHAIN, chain_err >= 3, now);
            core_unlock();
        }
        alive_bits |= ALIVE_ALARM;
    }
}

static void power_task(void *arg)
{
    (void)arg;
    power_meas_t m;
    for (;;) {
        bsp_power_read(&m);
        core_lock();
        alarm_app_power(&g_app, &m, app_now());
        bool link = netif_is_link_up(&gnetif);
        engine_set_fault(&g_app.engine, FAULT_ETH, !link, app_now());
        core_unlock();
        g_comm.eth_link = link;
        uint32_t ip = gnetif.ip_addr.addr;
        g_comm.ip[0] = (uint8_t)ip; g_comm.ip[1] = (uint8_t)(ip >> 8);
        g_comm.ip[2] = (uint8_t)(ip >> 16); g_comm.ip[3] = (uint8_t)(ip >> 24);
        bsp_audio_set_volume(g_cfg.volume);

        /* Écriture différée du journal sur microSD */
        alarm_event_t ev;
        while (xQueueReceive(persist_q, &ev, 0) == pdTRUE) {
            char line[180];
            core_lock();
            event_format(&ev, &g_cfg, line, sizeof line);
            core_unlock();
            storage_append_event_line(line);
        }
        alive_bits |= ALIVE_POWER;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void supervisor_task(void *arg)
{
    (void)arg;
    TickType_t last_ok = xTaskGetTickCount();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if ((alive_bits & ALIVE_ALL) == ALIVE_ALL) {
            alive_bits = 0;
            last_ok = xTaskGetTickCount();
        }
        /* Rafraîchit les chiens de garde tant que les tâches critiques vivent
         * (tolérance 3 s pour la tâche power qui accède à la carte SD) */
        if (xTaskGetTickCount() - last_ok < pdMS_TO_TICKS(3000)) {
            HAL_IWDG_Refresh(&hiwdg1);
            bsp_watchdog_kick();
        }
    }
}

void app_start(void)
{
    core_mtx = xSemaphoreCreateRecursiveMutex();
    save_mtx = xSemaphoreCreateMutex();
    persist_q = xQueueCreate(64, sizeof(alarm_event_t));

    if (!storage_load_config(&g_cfg, sizeof g_cfg)) {
        /* Première mise sous tension ou Flash corrompue : usine + sels aléatoires */
        config_defaults(&g_cfg);
        uint8_t salt[8];
        bsp_random(salt, 8); user_set_pin(&g_cfg.users[0], "0000", salt);
        bsp_random(salt, 8); user_set_pin(&g_cfg.users[1], "1234", salt);
        config_seal(&g_cfg);
        storage_save_config(&g_cfg, sizeof g_cfg);
    }

    bsp_zones_init();
    bsp_relays_init();
    bsp_power_init();
    bsp_audio_init();

    app_hooks_t h = { bsp_rtc_get_unix, hook_voice, hook_persist, NULL };
    alarm_app_init(&g_app, &g_cfg, &h);
    engine_set_fault(&g_app.engine, FAULT_RTC, true, 0);   /* jusqu'à la 1re synchro SNTP */

    xTaskCreate(supervisor_task, "superv", 256,  NULL, 7, NULL);
    xTaskCreate(alarm_task,      "alarm",  1024, NULL, 6, NULL);
    xTaskCreate(power_task,      "power",  1024, NULL, 4, NULL);
    xTaskCreate(bsp_audio_task,  "audio",  1024, NULL, 4, NULL);
    xTaskCreate(modem_task,      "modem",  2048, NULL, 3, NULL);
    xTaskCreate(net_task,        "net",    2048, NULL, 3, NULL);
    xTaskCreate(ui_task,         "ui",     4096, NULL, 2, NULL);
}
