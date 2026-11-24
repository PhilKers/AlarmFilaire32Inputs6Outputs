/**
 * @file event_log.c
 */
#include "event_log.h"
#include "alarm_config.h"
#include <stdio.h>
#include <string.h>

void event_log_init(event_log_t *l) { memset(l, 0, sizeof *l); }

void event_log_push(event_log_t *l, const alarm_event_t *ev)
{
    l->buf[l->head & (EVENT_LOG_SIZE - 1u)] = *ev;
    l->head++;
}

uint32_t event_log_count(const event_log_t *l)
{
    return l->head < EVENT_LOG_SIZE ? l->head : EVENT_LOG_SIZE;
}

bool event_log_get(const event_log_t *l, uint32_t idx, alarm_event_t *out)
{
    if (idx >= event_log_count(l)) return false;
    *out = l->buf[(l->head - 1u - idx) & (EVENT_LOG_SIZE - 1u)];
    return true;
}

/* Conversion UNIX → date civile (algorithme de H. Hinnant, sans libc). */
static void unix_to_civil(uint32_t t, int *y, int *mo, int *d, int *h, int *mi, int *s)
{
    uint32_t days = t / 86400u, rem = t % 86400u;
    *h = (int)(rem / 3600u); *mi = (int)((rem / 60u) % 60u); *s = (int)(rem % 60u);
    int32_t z = (int32_t)days + 719468;
    int32_t era = z / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yy = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *mo = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yy + (*mo <= 2));
}

int event_format(const alarm_event_t *ev, const void *cfgv, char *out, size_t len)
{
    const alarm_config_t *cfg = (const alarm_config_t *)cfgv;
    int y, mo, d, h, mi, s;
    unix_to_civil(ev->time, &y, &mo, &d, &h, &mi, &s);
    int n = snprintf(out, len, "%04d-%02d-%02d %02d:%02d:%02d %s", y, mo, d, h, mi, s,
                     event_label((event_type_t)ev->type));
    if (n < 0 || (size_t)n >= len) return n;

    const char *extra = NULL;
    char tmp[48];
    switch (ev->type) {
    case EV_ALARM_INTRUSION: case EV_ALARM_FIRE: case EV_ALARM_PANIC: case EV_ALARM_TAMPER:
    case EV_ALARM_TECHNICAL: case EV_ZONE_OPEN: case EV_ZONE_CLOSE: case EV_ZONE_TAMPER:
    case EV_ZONE_FAULT: case EV_CHIME: case EV_BYPASS_ON: case EV_BYPASS_OFF:
    case EV_AUTO_BYPASS: case EV_ENTRY_DELAY:
        if (ev->source < NB_ZONES && cfg) {
            snprintf(tmp, sizeof tmp, "Z%u %s", ev->source + 1u, cfg->zones[ev->source].name);
            extra = tmp;
        } else if (ev->source == SRC_BOX) {
            extra = "Coffret";
        }
        break;
    case EV_FAULT_ON: case EV_FAULT_OFF:
        extra = fault_label((fault_t)ev->source);
        break;
    case EV_OUTPUT_ON: case EV_OUTPUT_OFF:
        if (ev->source < NB_OUTPUTS && cfg) extra = cfg->outputs[ev->source].name;
        break;
    default:
        break;
    }
    if (extra) n += snprintf(out + n, len - (size_t)n, " - %s", extra);
    if (ev->user != USER_NONE && cfg && ev->user < NB_USERS && (size_t)n < len)
        n += snprintf(out + n, len - (size_t)n, " (%s)", cfg->users[ev->user].name);
    return n;
}
