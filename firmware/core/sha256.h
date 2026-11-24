/**
 * @file sha256.h
 * @brief SHA-256 compact (FIPS 180-4) pour le hachage des codes d'accès.
 *        Sur STM32H753 (variante crypto) on peut le remplacer par HAL_HASH.
 */
#ifndef SHA256_H
#define SHA256_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    uint32_t datalen;
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *c);
void sha256_update(sha256_ctx_t *c, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *c, uint8_t hash[32]);
void sha256(const uint8_t *data, size_t len, uint8_t hash[32]);
#endif
