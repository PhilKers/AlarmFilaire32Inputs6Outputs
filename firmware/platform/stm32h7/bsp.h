/**
 * @file bsp.h
 * @brief Interface des pilotes de la carte (couche plateforme STM32H7).
 */
#ifndef BSP_H
#define BSP_H
#include <stdint.h>
#include <stdbool.h>
#include "power_monitor.h"

/* Entrées */
void     bsp_zones_init(void);
/** Acquiert les 32 boucles. Retourne false si VZONE est hors tolérance. */
bool     bsp_zones_scan(uint16_t permille[32], uint16_t *vzone_mv);

/* Relais */
void     bsp_relays_init(void);
/** Écrit le mot 24 bits ; vérifie la chaîne par relecture (SER OUT). */
bool     bsp_relays_write(uint32_t word);

/* Alimentation */
void     bsp_power_init(void);
void     bsp_power_read(power_meas_t *m);
void     bsp_aux12_enable(bool on);     /* Réarmement détecteurs incendie */
bool     bsp_box_tamper_open(void);

/* Audio */
void     bsp_audio_init(void);
/** Ajoute une séquence de fragments à la file de lecture. */
void     bsp_audio_enqueue(const uint16_t *seq, unsigned n);
void     bsp_audio_set_volume(uint8_t percent);
void     bsp_audio_task(void *arg);

/* Stockage */
bool     storage_load_config(void *cfg, uint32_t size);
bool     storage_save_config(const void *cfg, uint32_t size);
void     storage_append_event_line(const char *line);

/* Temps */
uint32_t bsp_rtc_get_unix(void);
void     bsp_rtc_set_unix(uint32_t t);
void     bsp_random(uint8_t *buf, unsigned n);

/* Chien de garde externe */
void     bsp_watchdog_kick(void);
#endif
