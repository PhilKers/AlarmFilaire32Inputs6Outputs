/**
 * @file board.h
 * @brief Affectation des broches de la carte centrale (STM32H743BIT6, LQFP208).
 *
 * Référence : docs/03-brochage-stm32.md. Les périphériques à fonction
 * alternée (FMC, LTDC, ETH, SDMMC, QUADSPI, SAI, USART, I2C, SPI) sont
 * initialisés par le code généré par STM32CubeMX (fichier .ioc) ; ce fichier
 * ne liste que les GPIO pilotés directement par l'application.
 */
#ifndef BOARD_H
#define BOARD_H
#include "stm32h7xx_hal.h"

/* --- Multiplexeurs analogiques des boucles (2 × CD74HC4067) ------------ */
#define MUX_S0_PORT         GPIOD
#define MUX_S0_PIN          GPIO_PIN_4
#define MUX_S1_PIN          GPIO_PIN_5
#define MUX_S2_PIN          GPIO_PIN_6
#define MUX_S3_PIN          GPIO_PIN_7      /* PD4..PD7 : même port, écriture BSRR */
#define MUX_ADDR_SHIFT      4u
/* MUX A (zones 1-16)  → PA3 = ADC1_INP15
 * MUX B (zones 17-32) → PB0 = ADC2_INP9
 * VZONE/2             → PB1 = ADC1_INP5
 * VIN/6               → PA4 = ADC1_INP18
 * +5V/2               → PA0 = ADC1_INP16
 * +12V AUX/6          → PC0 = ADC1_INP10
 * +12V OUT/6          → PF10 = ADC3_INP6                                 */

/* --- Chaîne relais (3 × TPIC6B595 sur SPI1) ----------------------------- */
#define RLY_RCK_PORT        GPIOG           /* Verrou (RCK)                 */
#define RLY_RCK_PIN         GPIO_PIN_14
#define RLY_OE_PORT         GPIOG           /* /G actif bas, pull-up 10k    */
#define RLY_OE_PIN          GPIO_PIN_3

/* --- Module 4G SIMCom A7670E (USART3 via TXS0108E 3V3↔1V8) ------------- */
#define MODEM_PWRKEY_PORT   GPIOH           /* via NPN : 1 = PWRKEY à 0     */
#define MODEM_PWRKEY_PIN    GPIO_PIN_6
#define MODEM_RESET_PORT    GPIOH           /* via NPN : 1 = RESET actif    */
#define MODEM_RESET_PIN     GPIO_PIN_7
#define MODEM_STATUS_PORT   GPIOI
#define MODEM_STATUS_PIN    GPIO_PIN_8
#define MODEM_RI_PORT       GPIOH
#define MODEM_RI_PIN        GPIO_PIN_4
#define MODEM_DTR_PORT      GPIOI
#define MODEM_DTR_PIN       GPIO_PIN_11
#define MODEM_PWR_EN_PORT   GPIOE           /* EN du régulateur 3,8 V       */
#define MODEM_PWR_EN_PIN    GPIO_PIN_3

/* --- Écran / dalle tactile ---------------------------------------------- */
#define LCD_BL_EN_PORT      GPIOC
#define LCD_BL_EN_PIN       GPIO_PIN_13
#define LCD_PWR_EN_PORT     GPIOC           /* Alimentation logique dalle   */
#define LCD_PWR_EN_PIN      GPIO_PIN_7
#define LVDS_SHDN_PORT      GPIOC           /* /SHTDN du SN75LVDS83B        */
#define LVDS_SHDN_PIN       GPIO_PIN_6
/* LCD_BL_PWM : PA8 = TIM1_CH1 (20 kHz) */
#define TOUCH_INT_PORT      GPIOB
#define TOUCH_INT_PIN       GPIO_PIN_7
#define TOUCH_RST_PORT      GPIOB
#define TOUCH_RST_PIN       GPIO_PIN_4

/* --- Audio --------------------------------------------------------------- */
#define AMP_SD_PORT         GPIOE           /* SD_MODE MAX98357A (0 = arrêt)*/
#define AMP_SD_PIN          GPIO_PIN_2

/* --- Divers ----------------------------------------------------------- */
#define SD_DETECT_PORT      GPIOD
#define SD_DETECT_PIN       GPIO_PIN_11
#define PHY_RST_PORT        GPIOG
#define PHY_RST_PIN         GPIO_PIN_10
/* PHY_INT non câblé : la broche nINT/REFCLKO du LAN8742A fournit le 50 MHz */
#define AC_OK_PORT          GPIOA           /* Contact AC OK du bloc PSC    */
#define AC_OK_PIN           GPIO_PIN_15
#define BAT_LOW_PORT        GPIOB           /* Contact BAT LOW du bloc PSC  */
#define BAT_LOW_PIN         GPIO_PIN_15
#define BOX_TAMPER_PORT     GPIOG           /* Contact d'autoprotection     */
#define BOX_TAMPER_PIN      GPIO_PIN_9
#define AUX12_EN_PORT       GPIOG           /* Coupure +12 V détecteurs     */
#define AUX12_EN_PIN        GPIO_PIN_6
#define WDI_PORT            GPIOG           /* Chien de garde TPS3823       */
#define WDI_PIN             GPIO_PIN_7

/* --- Ponts diviseurs (rapport de mesure) -------------------------------- */
#define DIV_VZONE           2u      /* 10k / 10k          */
#define DIV_5V              2u      /* 10k / 10k          */
#define DIV_12V             6u      /* 51k / 10k ≈ 6,1    */
#define DIV_12V_EXACT       6.1f

/* --- Adresses I2C (7 bits) ---------------------------------------------- */
#define I2C_ADDR_GT911      0x5Du
#define I2C_ADDR_INA226     0x40u
#define I2C_ADDR_EEPROM     0x50u   /* 24AA025E48 : adresse MAC unique     */

#endif /* BOARD_H */
