/**
 * @file alarm_config.c
 * @brief Configuration d'usine, scellement CRC et validation.
 */
#include "alarm_config.h"
#include "users.h"
#include <string.h>
#include <stdio.h>

uint32_t crc32_compute(const void *data, size_t len)
{
    /* CRC-32 IEEE 802.3 (identique au périphérique CRC du STM32 configuré
     * en polynôme 0x04C11DB7 avec inversion d'entrée/sortie). */
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

void config_defaults(alarm_config_t *cfg)
{
    memset(cfg, 0, sizeof *cfg);
    cfg->magic   = CONFIG_MAGIC;
    cfg->version = CONFIG_VERSION;
    cfg->size    = (uint16_t)sizeof *cfg;
    strncpy(cfg->site_name, "Ma maison", NAME_LEN - 1);

    cfg->exit_delay_s       = 30;
    cfg->entry_delay_s      = 20;
    cfg->siren_duration_s   = 180;      /* 3 min (limite usuelle en France) */
    cfg->swinger_limit      = 3;
    cfg->volume             = 70;
    cfg->mains_fail_delay_s = 60;
    cfg->bat_low_mv         = 11500;
    cfg->bat_ok_mv          = 12200;
    cfg->voice_enabled      = 1;
    cfg->tamper_when_disarmed_siren = 0;
    cfg->utc_offset_min     = 60;

    /* Seuils en ‰ de VZONE pour R_pullup = 4k7, R_série = 470 Ω, EOL = 4k7 :
     *   court-circuit 91 ‰, repos 524 ‰, alarme 677 ‰, coupure 1000 ‰. */
    cfg->thresholds.short_max  = 300;
    cfg->thresholds.normal_max = 600;
    cfg->thresholds.alarm_max  = 850;

    for (unsigned i = 0; i < NB_ZONES; i++) {
        zone_cfg_t *z = &cfg->zones[i];
        snprintf(z->name, NAME_LEN, "Zone %u", i + 1);
        z->type        = ZT_DISABLED;
        z->wiring      = WIRING_DEOL;
        z->flags       = ZF_BYPASSABLE;
        z->response_ms = 300;
    }
    /* Exemple : zone 1 = porte d'entrée temporisée */
    strncpy(cfg->zones[0].name, "Porte d'entree", NAME_LEN - 1);
    cfg->zones[0].type = ZT_DELAYED;
    cfg->zones[0].flags |= ZF_CHIME;

    static const struct { const char *n; uint8_t m; uint8_t f; } outs[NB_OUTPUTS] = {
        { "Sirene interieure",  OUT_MODE_12V, OUT_FN_SIREN },
        { "Sirene exterieure",  OUT_MODE_12V, OUT_FN_SIREN },
        { "Flash",              OUT_MODE_12V, OUT_FN_STROBE },
        { "Report etat",        OUT_MODE_DRY, OUT_FN_ARMED },
        { "Report defaut",      OUT_MODE_DRY, OUT_FN_FAULT },
        { "Telecommande",       OUT_MODE_DRY, OUT_FN_REMOTE },
    };
    for (unsigned i = 0; i < NB_OUTPUTS; i++) {
        strncpy(cfg->outputs[i].name, outs[i].n, NAME_LEN - 1);
        cfg->outputs[i].mode     = outs[i].m;
        cfg->outputs[i].function = outs[i].f;
    }

    /* Codes d'usine — À CHANGER à la mise en service (l'écran le signale). */
    static const uint8_t salt_i[8] = { 0x49, 0x6E, 0x73, 0x74, 0x61, 0x6C, 0x6C, 0x31 };
    static const uint8_t salt_m[8] = { 0x4D, 0x61, 0x69, 0x74, 0x72, 0x65, 0x30, 0x31 };
    strncpy(cfg->users[0].name, "Installateur", 15);
    cfg->users[0].level = LEVEL_INSTALLER;
    cfg->users[0].enabled = 1;
    user_set_pin(&cfg->users[0], "0000", salt_i);
    strncpy(cfg->users[1].name, "Maitre", 15);
    cfg->users[1].level = LEVEL_MASTER;
    cfg->users[1].enabled = 1;
    user_set_pin(&cfg->users[1], "1234", salt_m);

    cfg->net.dhcp = 1;
    strncpy(cfg->net.ntp_server, "pool.ntp.org", sizeof cfg->net.ntp_server - 1);
    cfg->net.http_port = 80;
    cfg->apn[0] = 0;   /* APN de l operateur, a renseigner */

    config_seal(cfg);
}

void config_seal(alarm_config_t *cfg)
{
    cfg->crc32 = crc32_compute(cfg, offsetof(alarm_config_t, crc32));
}

bool config_is_valid(const alarm_config_t *cfg)
{
    if (cfg->magic != CONFIG_MAGIC || cfg->version != CONFIG_VERSION) return false;
    if (cfg->size != sizeof *cfg) return false;
    if (cfg->crc32 != crc32_compute(cfg, offsetof(alarm_config_t, crc32))) return false;
    const loop_thresholds_t *t = &cfg->thresholds;
    if (!(t->short_max < t->normal_max && t->normal_max < t->alarm_max && t->alarm_max < 1000))
        return false;
    for (unsigned i = 0; i < NB_ZONES; i++)
        if (cfg->zones[i].type > ZT_TAMPER || cfg->zones[i].wiring > WIRING_DEOL) return false;
    for (unsigned i = 0; i < NB_OUTPUTS; i++)
        if (cfg->outputs[i].mode > OUT_MODE_5V || cfg->outputs[i].function > OUT_FN_TECHNICAL)
            return false;
    return true;
}
