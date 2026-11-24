/**
 * @file bsp_audio.c
 * @brief Annonces vocales : WAV (16 kHz, 16 bits, mono) sur microSD →
 *        SAI1 bloc A (I2S maître, DMA circulaire) → ampli MAX98357A.
 *
 * Une tâche dédiée lit les fragments mis en file et remplit le tampon DMA
 * par moitiés (interruptions demi-transfert / transfert complet).
 */
#include "bsp.h"
#include "board.h"
#include "voice.h"
#include "ff.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <string.h>

extern SAI_HandleTypeDef hsai_BlockA1;  /* I2S 16 kHz, 16 bits, stéréo */

#define HALF_FRAMES     512u            /* 32 ms par demi-tampon        */
#define AUDIO_QUEUE_LEN 32u

/* Tampon DMA en RAM D2 (accessible au DMA1/2), non cacheable via la MPU */
static int16_t dma_buf[2 * HALF_FRAMES * 2] __attribute__((section(".dma_buffer"), aligned(32)));
static QueueHandle_t q_frag;
static TaskHandle_t  audio_task_handle;
static volatile uint8_t g_volume = 70;

void bsp_audio_init(void)
{
    q_frag = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(uint16_t));
    HAL_GPIO_WritePin(AMP_SD_PORT, AMP_SD_PIN, GPIO_PIN_RESET);    /* ampli éteint */
}

void bsp_audio_set_volume(uint8_t percent) { g_volume = percent > 100 ? 100 : percent; }

void bsp_audio_enqueue(const uint16_t *seq, unsigned n)
{
    for (unsigned i = 0; i < n; i++) xQueueSend(q_frag, &seq[i], 0);
}

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *h)
{
    (void)h;
    BaseType_t w = pdFALSE;
    xTaskNotifyFromISR(audio_task_handle, 1u, eSetBits, &w);
    portYIELD_FROM_ISR(w);
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *h)
{
    (void)h;
    BaseType_t w = pdFALSE;
    xTaskNotifyFromISR(audio_task_handle, 2u, eSetBits, &w);
    portYIELD_FROM_ISR(w);
}

/** Se positionne au début des données PCM. Retourne la taille, 0 si format refusé. */
static uint32_t wav_open(FIL *f)
{
    uint8_t h[12];
    UINT br;
    if (f_read(f, h, 12, &br) != FR_OK || br != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
        return 0;
    for (;;) {
        uint8_t ck[8];
        if (f_read(f, ck, 8, &br) != FR_OK || br != 8) return 0;
        uint32_t sz = ck[4] | (ck[5] << 8) | (ck[6] << 16) | ((uint32_t)ck[7] << 24);
        if (!memcmp(ck, "fmt ", 4)) {
            uint8_t fmt[16];
            if (f_read(f, fmt, 16, &br) != FR_OK) return 0;
            uint16_t ch = (uint16_t)(fmt[2] | (fmt[3] << 8));
            uint32_t rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            uint16_t bits = (uint16_t)(fmt[14] | (fmt[15] << 8));
            if (ch != 1 || rate != 16000 || bits != 16) return 0;
            f_lseek(f, f_tell(f) + sz - 16);
        } else if (!memcmp(ck, "data", 4)) {
            return sz;
        } else {
            f_lseek(f, f_tell(f) + sz + (sz & 1u));
        }
    }
}

/* Remplit une moitié de tampon ; complète par du silence. Retourne false en fin de fichier. */
static bool fill_half(FIL *f, uint32_t *remaining, int16_t *dst)
{
    static int16_t mono[HALF_FRAMES];
    UINT br = 0;
    uint32_t want = HALF_FRAMES * 2u;
    if (*remaining < want) want = *remaining;
    if (want) f_read(f, mono, want, &br);
    *remaining -= br;
    unsigned frames = br / 2u;
    int32_t vol = g_volume;
    for (unsigned i = 0; i < HALF_FRAMES; i++) {
        int16_t s = (i < frames) ? (int16_t)((mono[i] * vol) / 100) : 0;
        dst[2 * i] = s;           /* gauche */
        dst[2 * i + 1] = s;       /* droite */
    }
    return br > 0;
}

void bsp_audio_task(void *arg)
{
    (void)arg;
    audio_task_handle = xTaskGetCurrentTaskHandle();
    static FIL f;
    for (;;) {
        uint16_t id;
        xQueueReceive(q_frag, &id, portMAX_DELAY);

        HAL_GPIO_WritePin(AMP_SD_PORT, AMP_SD_PIN, GPIO_PIN_SET);   /* réveil ampli */
        vTaskDelay(pdMS_TO_TICKS(5));
        memset(dma_buf, 0, sizeof dma_buf);
        HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)dma_buf, (uint16_t)(sizeof dma_buf / 2u));

        do {    /* Enchaîne les fragments sans couper le flux I2S */
            char path[24];
            voice_filename(id, path, sizeof path);
            if (f_open(&f, path, FA_READ) != FR_OK) continue;
            uint32_t remaining = wav_open(&f);
            bool more = remaining > 0;
            while (more) {
                uint32_t bits = 0;
                xTaskNotifyWait(0, 3u, &bits, pdMS_TO_TICKS(100));
                if (bits & 1u) more = fill_half(&f, &remaining, &dma_buf[0]);
                if (bits & 2u) more = fill_half(&f, &remaining, &dma_buf[HALF_FRAMES * 2u]) && more;
            }
            f_close(&f);
        } while (xQueueReceive(q_frag, &id, pdMS_TO_TICKS(50)) == pdTRUE);

        vTaskDelay(pdMS_TO_TICKS(80));      /* vide le tampon (silence) */
        HAL_SAI_DMAStop(&hsai_BlockA1);
        HAL_GPIO_WritePin(AMP_SD_PORT, AMP_SD_PIN, GPIO_PIN_RESET);
    }
}
