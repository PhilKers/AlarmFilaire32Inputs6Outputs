/**
 * @file bsp_relays.c
 * @brief Chaîne de 3 registres TPIC6B595 (24 sorties open-drain 150 mA).
 *
 * SPI1 : SCK=PA5, MOSI=PB5 (SER IN du 1er registre), MISO=PA6 (SER OUT du
 * dernier registre). Mode 0, 1 MHz, MSB en premier.
 * Vérification d'intégrité : on envoie le mot, puis un second mot identique ;
 * les 24 bits ressortis par SER OUT pendant le second envoi doivent être égaux
 * au premier (le premier envoi les a chargés dans la chaîne). En cas d'écart,
 * le défaut FAULT_RELAY_CHAIN est levé par l'appelant.
 */
#include "bsp.h"
#include "board.h"

extern SPI_HandleTypeDef hspi1;

void bsp_relays_init(void)
{
    GPIO_InitTypeDef g = { 0 };
    __HAL_RCC_GPIOG_CLK_ENABLE();
    HAL_GPIO_WritePin(RLY_OE_PORT, RLY_OE_PIN, GPIO_PIN_SET);      /* sorties bloquées */
    HAL_GPIO_WritePin(RLY_RCK_PORT, RLY_RCK_PIN, GPIO_PIN_RESET);
    g.Pin = RLY_OE_PIN | RLY_RCK_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOG, &g);

    bsp_relays_write(0);                                           /* tout au repos */
    HAL_GPIO_WritePin(RLY_OE_PORT, RLY_OE_PIN, GPIO_PIN_RESET);    /* validation */
}

static void shift24(uint32_t word, uint32_t *readback)
{
    uint8_t tx[3] = { (uint8_t)(word >> 16), (uint8_t)(word >> 8), (uint8_t)word };
    uint8_t rx[3] = { 0 };
    HAL_SPI_TransmitReceive(&hspi1, tx, rx, 3, 5);
    if (readback) *readback = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | rx[2];
}

bool bsp_relays_write(uint32_t word)
{
    uint32_t rb = 0;
    word &= 0xFFFFFFu;
    shift24(word, NULL);
    shift24(word, &rb);              /* ressort le mot précédent = word */
    /* Impulsion de verrouillage : transfert simultané vers les 24 sorties */
    HAL_GPIO_WritePin(RLY_RCK_PORT, RLY_RCK_PIN, GPIO_PIN_SET);
    __NOP(); __NOP(); __NOP(); __NOP();
    HAL_GPIO_WritePin(RLY_RCK_PORT, RLY_RCK_PIN, GPIO_PIN_RESET);
    return rb == word;
}
