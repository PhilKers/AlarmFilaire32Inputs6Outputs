/**
 * @file bsp_zones.c
 * @brief Acquisition des 32 boucles : 2 multiplexeurs CD74HC4067 vers ADC1/ADC2.
 *
 * Mesure ratiométrique : chaque boucle est rapportée à VZONE (mesurée à
 * chaque cycle via un pont 1/2), ce qui rend la classification insensible à
 * la précision du régulateur VZONE et de VREF+.
 */
#include "bsp.h"
#include "board.h"

extern ADC_HandleTypeDef hadc1;     /* Généré par CubeMX : 16 bits, suréchantillonnage ×16 */
extern ADC_HandleTypeDef hadc2;

#define MUX_SETTLE_US   15u         /* RC d'entrée 10k/100nF déjà chargé : simple établissement */
#define VREF_MV         3300u
#define ADC_FULL        65535u

static void delay_us(uint32_t us)
{
    /* DWT->CYCCNT activé dans bsp_zones_init() */
    uint32_t start = DWT->CYCCNT, ticks = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < ticks) { }
}

uint16_t adc_read(ADC_HandleTypeDef *h, uint32_t channel)
{
    ADC_ChannelConfTypeDef c = { 0 };
    c.Channel      = channel;
    c.Rank         = ADC_REGULAR_RANK_1;
    c.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
    c.SingleDiff   = ADC_SINGLE_ENDED;
    c.OffsetNumber = ADC_OFFSET_NONE;
    HAL_ADC_ConfigChannel(h, &c);
    HAL_ADC_Start(h);
    if (HAL_ADC_PollForConversion(h, 2) != HAL_OK) return 0xFFFFu;
    return (uint16_t)HAL_ADC_GetValue(h);
}

static void mux_select(unsigned addr)
{
    /* Positionne S0..S3 (PD4..PD7) en une seule écriture atomique */
    uint32_t set = (addr & 0xFu) << MUX_ADDR_SHIFT;
    uint32_t clr = (~addr & 0xFu) << MUX_ADDR_SHIFT;
    MUX_S0_PORT->BSRR = set | (clr << 16);
}

void bsp_zones_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    GPIO_InitTypeDef g = { 0 };
    __HAL_RCC_GPIOD_CLK_ENABLE();
    g.Pin   = MUX_S0_PIN | MUX_S1_PIN | MUX_S2_PIN | MUX_S3_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(MUX_S0_PORT, &g);

    HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED);
    HAL_ADCEx_Calibration_Start(&hadc2, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED);
}

bool bsp_zones_scan(uint16_t permille[32], uint16_t *vzone_mv)
{
    uint32_t raw_vz = (uint32_t)adc_read(&hadc1, ADC_CHANNEL_5) * DIV_VZONE;   /* PB1 */
    *vzone_mv = (uint16_t)((raw_vz * VREF_MV) / ADC_FULL);
    if (*vzone_mv < 3000u || *vzone_mv > 3600u) return false;

    for (unsigned a = 0; a < 16u; a++) {
        mux_select(a);
        delay_us(MUX_SETTLE_US);
        uint32_t ra = adc_read(&hadc1, ADC_CHANNEL_15);   /* PA3 : zones 1-16  */
        uint32_t rb = adc_read(&hadc2, ADC_CHANNEL_9);    /* PB0 : zones 17-32 */
        permille[a]       = (uint16_t)((ra * 1000u) / raw_vz);
        permille[a + 16u] = (uint16_t)((rb * 1000u) / raw_vz);
    }
    return true;
}
