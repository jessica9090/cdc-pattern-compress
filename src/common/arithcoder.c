/*
 * common/arithcoder.c — arithmetic coder, encoder ACE and decoder ACD (see
 * arithcoder.h).
 */
#include "arithcoder.h"

/* ---------------------------------------------------------------- encoder */

void ace_init(ACE *e, Buf *out) {
    bw_init(&e->bw, out);
    e->low = 0; e->high = AC_TOP; e->follow = 0;
}

static void bit_plus_follow(ACE *e, int b) {
    bw_bits(&e->bw, (uint64_t)b, 1);
    for (; e->follow; e->follow--) bw_bits(&e->bw, (uint64_t)!b, 1);
}

void ace_encode(ACE *e, uint32_t cum, uint32_t freq, uint32_t total) {
    uint64_t range = e->high - e->low + 1;
    e->high = e->low + range * (cum + freq) / total - 1;
    e->low  = e->low + range * cum / total;
    for (;;) {
        if (e->high < AC_HALF) {
            bit_plus_follow(e, 0);
        } else if (e->low >= AC_HALF) {
            bit_plus_follow(e, 1);
            e->low -= AC_HALF; e->high -= AC_HALF;
        } else if (e->low >= AC_FIRST_QTR && e->high < AC_THIRD_QTR) {
            e->follow++;
            e->low -= AC_FIRST_QTR; e->high -= AC_FIRST_QTR;
        } else break;
        e->low = 2 * e->low;
        e->high = 2 * e->high + 1;
    }
}

void ace_flush(ACE *e) {
    e->follow++;
    bit_plus_follow(e, e->low < AC_FIRST_QTR ? 0 : 1);
    bw_flush(&e->bw);
}

/* ---------------------------------------------------------------- decoder */

void acd_init(ACD *d, const uint8_t *src, size_t len) {
    br_init(&d->br, src, len);
    d->low = 0; d->high = AC_TOP;
    d->value = br_bits(&d->br, AC_BITS);
}

uint32_t acd_get_freq(ACD *d, uint32_t total) {
    uint64_t range = d->high - d->low + 1;
    return (uint32_t)(((d->value - d->low + 1) * total - 1) / range);
}

void acd_advance(ACD *d, uint32_t cum, uint32_t freq, uint32_t total) {
    uint64_t range = d->high - d->low + 1;
    d->high = d->low + range * (cum + freq) / total - 1;
    d->low  = d->low + range * cum / total;
    for (;;) {
        if (d->high < AC_HALF) {
            /* nothing */
        } else if (d->low >= AC_HALF) {
            d->value -= AC_HALF; d->low -= AC_HALF; d->high -= AC_HALF;
        } else if (d->low >= AC_FIRST_QTR && d->high < AC_THIRD_QTR) {
            d->value -= AC_FIRST_QTR; d->low -= AC_FIRST_QTR; d->high -= AC_FIRST_QTR;
        } else break;
        d->low = 2 * d->low;
        d->high = 2 * d->high + 1;
        d->value = 2 * d->value + br_bits(&d->br, 1);
    }
}
