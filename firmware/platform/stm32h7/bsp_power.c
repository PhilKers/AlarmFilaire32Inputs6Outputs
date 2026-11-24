/**
 * @file bsp_power.c
 * @brief Mesures de l'alimentation secourue : INA226 (batterie) + ADC (rails).
 */
#include "bsp.h"
#include "board.h"

extern ADC_HandleTypeDef hadc1;
extern ADC_HandleTypeDef hadc3;
extern I2C_HandleTypeDef hi2c4;

uint16_t adc_read(ADC_HandleTypeDef *h, uint32_t channel);   /* bsp_zones.c */
extern volatile uint16_t g_vzone_mv;                          /* app_main.c  */

#define INA_REG_CONFIG  0x00u
#define INA_REG_SHUNT   0x01u
#define INA_REG_BUS     0x02u
#define INA_REG_CURRENT 0x04u
#define INA_REG_CAL     0x05u
/* Shunt 10 mΩ, LSB courant 1 mA → CAL = 0,00512 / (1e-3 × 0,01) = 512 */
#define INA_CAL_VALUE   512u
/* Moyenne 64, 1,1 ms bus et shunt, mode continu */
#define INA_CONFIG      0x4727u

static bool ina_write(uint8_t reg, uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    return HAL_I2C_Mem_Write(&hi2c4, I2C_ADDR_INA226 << 1, reg, 1, b, 2, 10) == HAL_OK;
}

static bool ina_read(uint8_t reg, uint16_t *v)
{
    uint8_t b[2];
    if (HAL_I2C_Mem_Read(&hi2c4, I2C_ADDR_INA226 << 1, reg, 1, b, 2, 10) != HAL_OK) return false;
    *v = (uint16_t)((b[0] << 8) | b[1]);
    return true;
}

static uint16_t rail_mv(ADC_HandleTypeDef *h, uint32_t ch, float div)
{
    uint32_t raw = adc_read(h, ch);
    return (uint16_t)((float)raw * 3300.0f / 65535.0f * div);
}

void bsp_power_init(void)
{
    GPIO_InitTypeDef g = { 0 };
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    /* Contacts secs du bloc PSC et autoprotection : pull-up externes 10k */
    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_NOPULL;
    g.Pin = AC_OK_PIN;      HAL_GPIO_Init(AC_OK_PORT, &g);
    g.Pin = BAT_LOW_PIN;    HAL_GPIO_Init(BAT_LOW_PORT, &g);
    g.Pin = BOX_TAMPER_PIN; HAL_GPIO_Init(BOX_TAMPER_PORT, &g);

    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pin = AUX12_EN_PIN | WDI_PIN;
    HAL_GPIO_Init(GPIOG, &g);
    bsp_aux12_enable(true);

    ina_write(INA_REG_CAL, INA_CAL_VALUE);
    ina_write(INA_REG_CONFIG, INA_CONFIG);
}

void bsp_power_read(power_meas_t *m)
{
    uint16_t v;
    /* Contact AC OK fermé (relais PSC au travail) = entrée à 0 */
    m->ac_ok = HAL_GPIO_ReadPin(AC_OK_PORT, AC_OK_PIN) == GPIO_PIN_RESET;

    m->vbat_mv = ina_read(INA_REG_BUS, &v) ? (uint16_t)(((uint32_t)v * 125u) / 100u) : 0;
    m->ibat_ma = ina_read(INA_REG_CURRENT, &v) ? (int16_t)v : 0;

    m->vin_mv    = rail_mv(&hadc1, ADC_CHANNEL_18, DIV_12V_EXACT);  /* PA4  */
    m->v5_mv     = rail_mv(&hadc1, ADC_CHANNEL_16, (float)DIV_5V); /* PA0  */
    m->vaux_mv   = rail_mv(&hadc1, ADC_CHANNEL_10, DIV_12V_EXACT);  /* PC0  */
    m->vout12_mv = rail_mv(&hadc3, ADC_CHANNEL_6, DIV_12V_EXACT);   /* PF10 */
    m->vzone_mv  = g_vzone_mv;
}

void bsp_aux12_enable(bool on)
{
    HAL_GPIO_WritePin(AUX12_EN_PORT, AUX12_EN_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool bsp_box_tamper_open(void)
{
    /* Microrupteur NF vers GND : couvercle fermé = 0, ouvert = 1 (pull-up) */
    return HAL_GPIO_ReadPin(BOX_TAMPER_PORT, BOX_TAMPER_PIN) == GPIO_PIN_SET;
}

void bsp_watchdog_kick(void)
{
    HAL_GPIO_TogglePin(WDI_PORT, WDI_PIN);   /* TPS3823 : toute transition réarme */
}
