/**
 * @file app_main.h
 * @brief Objets partagés entre les tâches FreeRTOS.
 */
#ifndef APP_MAIN_H
#define APP_MAIN_H
#include "alarm_app.h"

extern alarm_config_t g_cfg;
extern alarm_app_t    g_app;

/** Exclusion mutuelle d'accès au noyau (g_app, g_cfg). */
void core_lock(void);
void core_unlock(void);
ms_t app_now(void);

/** Enregistre la configuration (après modification par l'IHM ou le réseau). */
bool app_save_config(void);

/* Informations d'état des communications (affichage) */
typedef struct {
    int8_t  rssi_dbm;       /**< -113..-51, 0 = inconnu */
    bool    registered;
    bool    sim_ready;
    char    operator_name[24];
    bool    eth_link;
    uint8_t ip[4];
} comm_status_t;
extern volatile comm_status_t g_comm;
#endif
