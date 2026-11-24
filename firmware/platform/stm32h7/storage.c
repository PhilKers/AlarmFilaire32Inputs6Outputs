/**
 * @file storage.c
 * @brief Persistance : configuration en Flash QSPI W25Q128JV (double copie
 *        avec numéro de génération, résiste à une coupure pendant l'écriture)
 *        et journal des événements en CSV sur microSD.
 */
#include "bsp.h"
#include "alarm_config.h"
#include "ff.h"
#include <string.h>

extern QSPI_HandleTypeDef hqspi;

#define W25_WREN        0x06u
#define W25_RDSR1       0x05u
#define W25_SE4K        0x20u
#define W25_PP          0x02u
#define W25_READ        0x03u
#define SECTOR_SIZE     4096u
#define PAGE_SIZE       256u
/* Zone de configuration : 2 emplacements de 8 Ko en fin de Flash (16 Mo) */
#define CFG_SLOT_ADDR(n) (0x00FF0000u + (n) * 0x2000u)

typedef struct { uint32_t magic, gen, len, crc; } slot_hdr_t;
#define SLOT_MAGIC 0x43464731u   /* "CFG1" */

static void cmd_only(uint8_t ins)
{
    QSPI_CommandTypeDef c = { 0 };
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction = ins;
    HAL_QSPI_Command(&hqspi, &c, 100);
}

static void wait_ready(void)
{
    QSPI_CommandTypeDef c = { 0 };
    QSPI_AutoPollingTypeDef p = { 0 };
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction = W25_RDSR1;
    c.DataMode = QSPI_DATA_1_LINE;
    p.Match = 0; p.Mask = 0x01; p.MatchMode = QSPI_MATCH_MODE_AND;
    p.StatusBytesSize = 1; p.Interval = 0x10; p.AutomaticStop = QSPI_AUTOMATIC_STOP_ENABLE;
    HAL_QSPI_AutoPolling(&hqspi, &c, &p, 1000);
}

static void flash_read(uint32_t addr, void *dst, uint32_t len)
{
    QSPI_CommandTypeDef c = { 0 };
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction = W25_READ;
    c.AddressMode = QSPI_ADDRESS_1_LINE;
    c.AddressSize = QSPI_ADDRESS_24_BITS;
    c.Address = addr;
    c.DataMode = QSPI_DATA_1_LINE;
    c.NbData = len;
    HAL_QSPI_Command(&hqspi, &c, 100);
    HAL_QSPI_Receive(&hqspi, dst, 1000);
}

static void flash_erase4k(uint32_t addr)
{
    cmd_only(W25_WREN);
    QSPI_CommandTypeDef c = { 0 };
    c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    c.Instruction = W25_SE4K;
    c.AddressMode = QSPI_ADDRESS_1_LINE;
    c.AddressSize = QSPI_ADDRESS_24_BITS;
    c.Address = addr;
    HAL_QSPI_Command(&hqspi, &c, 100);
    wait_ready();
}

static void flash_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    while (len) {
        uint32_t chunk = PAGE_SIZE - (addr % PAGE_SIZE);
        if (chunk > len) chunk = len;
        cmd_only(W25_WREN);
        QSPI_CommandTypeDef c = { 0 };
        c.InstructionMode = QSPI_INSTRUCTION_1_LINE;
        c.Instruction = W25_PP;
        c.AddressMode = QSPI_ADDRESS_1_LINE;
        c.AddressSize = QSPI_ADDRESS_24_BITS;
        c.Address = addr;
        c.DataMode = QSPI_DATA_1_LINE;
        c.NbData = chunk;
        HAL_QSPI_Command(&hqspi, &c, 100);
        HAL_QSPI_Transmit(&hqspi, (uint8_t *)src, 100);
        wait_ready();
        addr += chunk; src += chunk; len -= chunk;
    }
}

static bool slot_valid(unsigned n, uint32_t size, slot_hdr_t *h, void *cfg)
{
    flash_read(CFG_SLOT_ADDR(n), h, sizeof *h);
    if (h->magic != SLOT_MAGIC || h->len != size) return false;
    flash_read(CFG_SLOT_ADDR(n) + sizeof *h, cfg, size);
    return crc32_compute(cfg, size) == h->crc;
}

bool storage_load_config(void *cfg, uint32_t size)
{
    slot_hdr_t h0, h1;
    bool v1 = slot_valid(1, size, &h1, cfg);
    bool v0 = slot_valid(0, size, &h0, cfg);         /* cfg contient le slot 0 */
    if (v0 && (!v1 || h0.gen >= h1.gen)) return config_is_valid(cfg);
    if (v1) { slot_valid(1, size, &h1, cfg); return config_is_valid(cfg); }
    return false;
}

bool storage_save_config(const void *cfg, uint32_t size)
{
    static uint8_t tmp[sizeof(alarm_config_t)];
    slot_hdr_t h0, h1;
    bool v0 = slot_valid(0, size, &h0, tmp);
    bool v1 = slot_valid(1, size, &h1, tmp);
    uint32_t g0 = v0 ? h0.gen : 0, g1 = v1 ? h1.gen : 0;
    unsigned target = (g0 <= g1) ? 0u : 1u;          /* écrase la plus ancienne */
    slot_hdr_t h = { SLOT_MAGIC, (g0 > g1 ? g0 : g1) + 1u, size, crc32_compute(cfg, size) };

    uint32_t addr = CFG_SLOT_ADDR(target);
    for (uint32_t off = 0; off < sizeof h + size; off += SECTOR_SIZE) flash_erase4k(addr + off);
    flash_write(addr + sizeof h, cfg, size);
    flash_write(addr, (const uint8_t *)&h, sizeof h);   /* en-tête en dernier */
    slot_hdr_t chk;
    return slot_valid(target, size, &chk, tmp);
}

void storage_append_event_line(const char *line)
{
    static FIL f;
    if (f_open(&f, "/JOURNAL.CSV", FA_OPEN_APPEND | FA_WRITE) != FR_OK) return;
    UINT bw;
    f_write(&f, line, (UINT)strlen(line), &bw);
    f_write(&f, "\r\n", 2, &bw);
    f_close(&f);
}
