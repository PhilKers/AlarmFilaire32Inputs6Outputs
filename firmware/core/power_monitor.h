/**
 * @file power_monitor.h
 * @brief Surveillance de l'alimentation secourue (secteur, batterie, fusibles).
 */
#ifndef POWER_MONITOR_H
#define POWER_MONITOR_H
#include "alarm_engine.h"

typedef struct {
    bool     ac_ok;         /**< Contact « AC OK » du bloc PSC                */
    uint16_t vin_mv;        /**< Tension bus d'entrée (après diode idéale)    */
    uint16_t vbat_mv;       /**< Tension batterie (INA226)                    */
    int16_t  ibat_ma;       /**< Courant batterie (+ = charge, - = décharge)  */
    uint16_t v5_mv;
    uint16_t vaux_mv;       /**< +12 V auxiliaire détecteurs                  */
    uint16_t vout12_mv;     /**< Bus +12 V des sorties                        */
    uint16_t vzone_mv;      /**< Référence des boucles                        */
} power_meas_t;

typedef struct {
    ms_t mains_lost_since;
    bool mains_lost;
    ms_t bat_low_since;
} power_monitor_t;

void power_monitor_init(power_monitor_t *p);
void power_monitor_update(power_monitor_t *p, const alarm_config_t *cfg,
                          alarm_engine_t *e, const power_meas_t *m, ms_t now);
#endif
