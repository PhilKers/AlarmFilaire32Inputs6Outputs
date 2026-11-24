/**
 * @file users.c
 */
#include "users.h"
#include "sha256.h"
#include <string.h>

bool pin_format_ok(const char *pin)
{
    if (pin == NULL) return false;
    size_t n = strlen(pin);
    if (n < PIN_MIN_LEN || n > PIN_MAX_LEN) return false;
    for (size_t i = 0; i < n; i++)
        if (pin[i] < '0' || pin[i] > '9') return false;
    return true;
}

static void hash_pin(const uint8_t salt[8], const char *pin, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, salt, 8);
    sha256_update(&c, (const uint8_t *)pin, strlen(pin));
    sha256_final(&c, out);
}

bool user_set_pin(user_cfg_t *u, const char *pin, const uint8_t salt[8])
{
    if (!pin_format_ok(pin)) return false;
    memcpy(u->salt, salt, 8);
    hash_pin(u->salt, pin, u->hash);
    return true;
}

/* Comparaison en temps constant (évite les attaques temporelles). */
static bool ct_equal(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}

auth_result_t users_authenticate(const alarm_config_t *cfg, auth_guard_t *g,
                                 const char *pin, ms_t now, int *user_index)
{
    if (g->locked) {
        if (!time_reached(now, g->locked_until)) return AUTH_LOCKED;
        g->locked = false;
        g->fails = 0;
    }
    if (!pin_format_ok(pin)) return AUTH_INVALID_FORMAT;

    int found = -1;
    for (unsigned i = 0; i < NB_USERS; i++) {
        const user_cfg_t *u = &cfg->users[i];
        if (!u->enabled || u->level == LEVEL_NONE) continue;
        uint8_t h[32];
        hash_pin(u->salt, pin, h);
        if (ct_equal(h, u->hash, 32) && found < 0) found = (int)i;
    }
    if (found >= 0) {
        g->fails = 0;
        if (user_index) *user_index = found;
        return AUTH_OK;
    }
    if (++g->fails >= AUTH_MAX_FAILS) {
        g->locked = true;
        g->locked_until = now + AUTH_LOCKOUT_MS;
    }
    return AUTH_BAD_CODE;
}
