# 3. Brochage du STM32H743BIT6 (LQFP208)

Affectation complète des broches utilisées. Les numéros de fonction
alternée (AF) sont ceux du datasheet STM32H743 (DS12110).

> ✅ **À faire avant le routage** : saisir ce tableau dans **STM32CubeMX**
> (projet `.ioc`) pour valider l'absence de conflit et générer le code
> d'initialisation. CubeMX signale immédiatement toute broche ou AF
> incompatible avec le boîtier.

## Mémoire externe — FMC SDRAM (AF12)

| Fonction | Broches |
|---|---|
| A0…A5 | PF0, PF1, PF2, PF3, PF4, PF5 |
| A6…A9 | PF12, PF13, PF14, PF15 |
| A10, A11 | PG0, PG1 |
| BA0, BA1 | PG4, PG5 |
| D0, D1, D2, D3 | PD14, PD15, PD0, PD1 |
| D4…D12 | PE7, PE8, PE9, PE10, PE11, PE12, PE13, PE14, PE15 |
| D13, D14, D15 | PD8, PD9, PD10 |
| D16…D23 | PH8, PH9, PH10, PH11, PH12, PH13, PH14, PH15 |
| D24…D27 | PI0, PI1, PI2, PI3 |
| D28, D29, D30, D31 | PI6, PI7, PI9, PI10 |
| NBL0…NBL3 | PE0, PE1, PI4, PI5 |
| SDCLK, SDCKE0, SDNE0 | PG8, PH2, PH3 |
| SDNRAS, SDNCAS, SDNWE | PF11, PG15, PH5 |

## Écran — LTDC (AF14)

| Fonction | Broches |
|---|---|
| R0…R7 | PI15, PJ0, PJ1, PJ2, PJ3, PJ4, PJ5, PJ6 |
| G0…G7 | PJ7, PJ8, PJ9, PJ10, PJ11, PK0, PK1, PK2 |
| B0…B7 | PJ12, PJ13, PJ14, PJ15, PK3, PK4, PK5, PK6 |
| HSYNC, VSYNC, CLK, DE | PI12, PI13, PI14, PK7 |
| Rétroéclairage PWM | PA8 — TIM1_CH1 (AF1) |
| BL_EN, LCD_PWR_EN, LVDS_SHDN | PC13, PC7, PC6 (GPIO sorties) |

## Tactile — I2C1 (AF4)

| Fonction | Broche |
|---|---|
| SCL, SDA | PB8, PB9 |
| TOUCH_INT (EXTI) | PB7 |
| TOUCH_RST | PB4 (NJTRST libéré : débogage en SWD uniquement) |

## Ethernet — RMII (AF11)

| Fonction | Broche |
|---|---|
| REF_CLK | PA1 |
| MDIO, MDC | PA2, PC1 |
| CRS_DV | PA7 |
| RXD0, RXD1 | PC4, PC5 |
| TX_EN, TXD0, TXD1 | PG11, PG13, PG12 |
| PHY_RST | PG10 (GPIO) |

## Stockage

| Fonction | Broches | AF |
|---|---|---|
| QUADSPI CLK, NCS | PB2, PB6 | AF9, AF10 |
| QUADSPI IO0…IO3 | PF8, PF9, PF7, PF6 | AF10, AF10, AF9, AF9 |
| SDMMC1 D0…D3 | PC8, PC9, PC10, PC11 | AF12 |
| SDMMC1 CK, CMD | PC12, PD2 | AF12 |
| SD_DETECT | PD11 (GPIO entrée) | — |
| I2C4 SCL, SDA (INA226, EEPROM MAC) | PD12, PD13 | AF4 |

## Audio — SAI1 bloc A (AF6)

| Fonction | Broche |
|---|---|
| FS_A, SCK_A, SD_A | PE4, PE5, PE6 |
| AMP_SD (SD_MODE) | PE2 (GPIO) |

## Module 4G — USART3 (AF7)

| Fonction | Broche |
|---|---|
| TX, RX | PB10, PB11 |
| CTS, RTS | PB13, PB14 |
| PWRKEY, RESET (via transistors) | PH6, PH7 |
| STATUS, RI, DTR | PI8, PH4, PI11 |
| MODEM_PWR_EN (EN régulateur 3,8 V) | PE3 |

## Entrées de zones — ADC

| Fonction | Broche | Canal |
|---|---|---|
| Multiplexeur A (zones 1-16) | PA3 | ADC1_INP15 |
| Multiplexeur B (zones 17-32) | PB0 | ADC2_INP9 |
| VZONE ÷ 2 | PB1 | ADC1_INP5 |
| Adresse S0…S3 | PD4, PD5, PD6, PD7 | GPIO sorties |

## Alimentation et surveillance

| Fonction | Broche | Canal / type |
|---|---|---|
| VIN ÷ 6,1 | PA4 | ADC1_INP18 |
| +5V ÷ 2 | PA0 | ADC1_INP16 |
| +12V_AUX ÷ 6,1 | PC0 | ADC1_INP10 |
| +12V_OUT ÷ 6,1 | PF10 | ADC3_INP6 |
| AC_OK (contact PSC) | PA15 | GPIO entrée |
| BAT_LOW (contact PSC) | PB15 | GPIO entrée |
| Autoprotection coffret | PG9 | GPIO entrée |
| AUX12_EN | PG6 | GPIO sortie |
| WDI (chien de garde externe) | PG7 | GPIO sortie |

## Sorties relais — SPI1 (AF5)

| Fonction | Broche |
|---|---|
| SCK, MOSI, MISO (relecture) | PA5, PB5, PA6 |
| RCK (verrou), /G (validation) | PG14, PG3 |

## Débogage et maintenance

| Fonction | Broche |
|---|---|
| SWDIO, SWCLK, SWO | PA13, PA14, PB3 |
| USART1 TX, RX (console) | PA9, PA10 (AF7) |
| USB OTG FS D−, D+ | PA11, PA12 (AF10) |
| HSE, LSE | PH0/PH1, PC14/PC15 |
| BOOT0, NRST, VBAT | broches dédiées |

## Broches libres (réserve)

Toutes les broches des ports F, G, H, I, J, K sont utilisées. Restent libres :
**PB12**, **PD3**, et **PC2 / PC3** (selon le boîtier, ces broches existent en variante
analogique `PC2_C / PC3_C` : à vérifier dans CubeMX avant de les employer).
Le bus I2C4 est aussi ramené sur un connecteur d'extension J30 (3V3, GND, SCL,
SDA) pour un futur clavier déporté ou lecteur de badges.
