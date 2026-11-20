/*
 * common/bitio.c — MSB-first bit writer with Elias-gamma, and the matching
 * bit reader (see bitio.h).
 */
#include "bitio.h"

/* ---- Bit writer ---- */
void bw_init(BW *w, Buf *b) { w->buf = b; w->acc = 0; w->nb = 0; }

void bw_bits(BW *w, uint64_t v, int n) {
    w->acc = (w->acc << n) | (v & ((1ULL << n) - 1));
    w->nb += n;
    while (w->nb >= 8) {
        w->nb -= 8;
        buf_u8(w->buf, (uint8_t)(w->acc >> w->nb));
    }
}

void bw_elias_gamma(BW *w, uint64_t v) {
    int len = 0;
    uint64_t tmp = v;
    while (tmp > 1) { tmp >>= 1; len++; }
    for (int i = 0; i < len; i++) bw_bits(w, 0, 1);
    bw_bits(w, v, len + 1);
}

void bw_flush(BW *w) {
    if (w->nb > 0) {
        buf_u8(w->buf, (uint8_t)(w->acc << (8 - w->nb)));
        w->nb = 0;
        w->acc = 0;
    }
}

/* ---- Bit reader ---- */
void br_init(BR *r, const uint8_t *d, size_t len) {
    r->d = d; r->len = len; r->pos = 0; r->acc = 0; r->nb = 0; r->over = 0;
}

uint64_t br_bits(BR *r, int n) {
    if (n == 0) return 0;
    while (r->nb < n) {
        uint8_t b = 0;
        if (r->pos < r->len) b = r->d[r->pos++];
        else r->over = 1;
        r->acc = (r->acc << 8) | b;
        r->nb += 8;
    }
    r->nb -= n;
    return (r->acc >> r->nb) & ((1ULL << n) - 1);
}

uint64_t br_elias_gamma(BR *r) {
    int len = 0;
    while (br_bits(r, 1) == 0) {
        if (++len > 57 || r->over) return 0;
    }
    return ((uint64_t)1 << len) | br_bits(r, len);
}
