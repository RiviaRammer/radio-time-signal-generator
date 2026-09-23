#include "bpc_protocol.h"

static unsigned parity(const uint8_t *q, unsigned first, unsigned last)
{
    unsigned p = 0;
    for (unsigned i = first; i <= last; ++i) p ^= (q[i] >> 1) ^ (q[i] & 1);
    return p & 1;
}

bool bpc_encode(const struct tm *t, uint16_t off_ms[20])
{
    if (!t || !off_ms || t->tm_year < 100 || t->tm_year > 199 ||
        t->tm_mon < 0 || t->tm_mon > 11 || t->tm_mday < 1 || t->tm_mday > 31 ||
        t->tm_hour < 0 || t->tm_hour > 23 || t->tm_min < 0 || t->tm_min > 59 ||
        t->tm_sec < 0 || t->tm_sec > 59 || t->tm_wday < 0 || t->tm_wday > 6) return false;
    const unsigned y = t->tm_year - 100;
    const unsigned h = t->tm_hour % 12;
    const unsigned w = t->tm_wday == 0 ? 7 : t->tm_wday;
    const unsigned m = t->tm_min, d = t->tm_mday, month = t->tm_mon + 1;
    uint8_t q[20] = {0, t->tm_sec / 20, 0,
        h >> 2, h & 3, m >> 4, (m >> 2) & 3, m & 3, w >> 2, w & 3, 0,
        d >> 4, (d >> 2) & 3, d & 3, month >> 2, month & 3,
        (y >> 4) & 3, (y >> 2) & 3, y & 3, 0};
    q[10] = ((t->tm_hour >= 12) << 1) | parity(q, 1, 9);
    q[19] = ((y >> 6) << 1) | parity(q, 11, 18);
    off_ms[0] = 0;
    for (unsigned i = 1; i < 20; ++i) off_ms[i] = (q[i] + 1) * 100;
    return true;
}

bool bpc_make_edges(const uint16_t off_ms[20], bpc_edge_t out[39])
{
    if (!off_ms || !out || off_ms[0] != 0) return false;
    out[0] = (bpc_edge_t){0, true};
    for (unsigned s = 1; s < 20; ++s) {
        if (off_ms[s] < 100 || off_ms[s] > 400 || off_ms[s] % 100) return false;
        out[2 * s - 1] = (bpc_edge_t){s * 1000000, false};
        out[2 * s] = (bpc_edge_t){s * 1000000 + off_ms[s] * 1000, true};
    }
    return true;
}
