/**
 * @file bsp_display.c
 * @brief Écran 10,4" 1024×768 : LTDC (RGB888) → SN75LVDS83B (LVDS) → dalle,
 *        double tampon RGB565 en SDRAM, portage LVGL v9, tactile capacitif GT911.
 *
 * Synchronisation dalle (XGA, horloge pixel 65 MHz, 60 Hz) — paramètres LTDC
 * générés par CubeMX : HSW 136, HBP 160, HFP 24 ; VSW 6, VBP 29, VFP 3.
 * Bande passante SDRAM : 1024×768×2 o×60 = 94 Mo/s en lecture LTDC, la SDRAM
 * 32 bits à 100 MHz en offre ~ 300 Mo/s utiles.
 */
#include "board.h"
#include "lvgl.h"
#include "FreeRTOS.h"
#include "task.h"

extern LTDC_HandleTypeDef hltdc;     /* Couche 0, RGB565, adresse = FB0   */
extern I2C_HandleTypeDef  hi2c1;
extern TIM_HandleTypeDef  htim1;     /* PWM rétroéclairage, CH1, 20 kHz   */

#define LCD_W   1024u
#define LCD_H   768u
#define FB_SIZE (LCD_W * LCD_H * 2u)
#define FB0     ((uint8_t *)0xC0000000u)
#define FB1     ((uint8_t *)(0xC0000000u + FB_SIZE))

static lv_display_t *disp;

/* ---- Affichage ------------------------------------------------------- */
static void flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    if (!lv_display_flush_is_last(d)) { lv_display_flush_ready(d); return; }
    /* Mode DIRECT : on bascule la couche sur le tampon rendu, prise en
     * compte au prochain retour vertical (pas de déchirement). */
    SCB_CleanDCache_by_Addr((uint32_t *)px_map, (int32_t)FB_SIZE);
    HAL_LTDC_SetAddress_NoReload(&hltdc, (uint32_t)px_map, 0);
    HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING);
}

void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *h)
{
    (void)h;
    lv_display_flush_ready(disp);
}

void bsp_backlight_set(uint8_t percent)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1,
                          (__HAL_TIM_GET_AUTORELOAD(&htim1) + 1u) * percent / 100u);
}

/* ---- Tactile GT911 ---------------------------------------------------- */
static bool gt911_read(uint16_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1, I2C_ADDR_GT911 << 1, reg, I2C_MEMADD_SIZE_16BIT,
                            buf, len, 10) == HAL_OK;
}

static void gt911_write8(uint16_t reg, uint8_t v)
{
    HAL_I2C_Mem_Write(&hi2c1, I2C_ADDR_GT911 << 1, reg, I2C_MEMADD_SIZE_16BIT, &v, 1, 10);
}

static void gt911_reset(void)
{
    /* Sélection de l'adresse 0x5D : INT à 0 pendant la sortie de reset */
    GPIO_InitTypeDef g = { 0 };
    g.Pin = TOUCH_INT_PIN; g.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(TOUCH_INT_PORT, &g);
    HAL_GPIO_WritePin(TOUCH_INT_PORT, TOUCH_INT_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_RESET);
    vTaskDelay(pdMS_TO_TICKS(10));
    HAL_GPIO_WritePin(TOUCH_RST_PORT, TOUCH_RST_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(60));
    g.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(TOUCH_INT_PORT, &g);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    static int16_t lx, ly;
    uint8_t st = 0, pt[4];
    data->state = LV_INDEV_STATE_RELEASED;
    if (gt911_read(0x814E, &st, 1) && (st & 0x80u)) {
        if ((st & 0x0Fu) > 0 && gt911_read(0x8150, pt, 4)) {
            lx = (int16_t)(pt[0] | (pt[1] << 8));
            ly = (int16_t)(pt[2] | (pt[3] << 8));
            data->state = LV_INDEV_STATE_PRESSED;
        }
        gt911_write8(0x814E, 0);
    }
    data->point.x = lx;
    data->point.y = ly;
}

static uint32_t tick_cb(void) { return xTaskGetTickCount(); }

void bsp_display_init(void)
{
    /* Séquence d'alimentation de la dalle : logique → LVDS → rétroéclairage */
    HAL_GPIO_WritePin(LCD_PWR_EN_PORT, LCD_PWR_EN_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(20));
    HAL_GPIO_WritePin(LVDS_SHDN_PORT, LVDS_SHDN_PIN, GPIO_PIN_SET);
    vTaskDelay(pdMS_TO_TICKS(200));
    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    bsp_backlight_set(80);
    HAL_GPIO_WritePin(LCD_BL_EN_PORT, LCD_BL_EN_PIN, GPIO_PIN_SET);

    gt911_reset();

    lv_init();
    lv_tick_set_cb(tick_cb);
    disp = lv_display_create(LCD_W, LCD_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, FB0, FB1, FB_SIZE, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t *ind = lv_indev_create();
    lv_indev_set_type(ind, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ind, touch_read_cb);
}
