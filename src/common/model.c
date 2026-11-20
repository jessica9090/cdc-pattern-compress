/*
 * common/model.c — adaptive frequency table AF (see model.h).
 */
#include "model.h"
#include <stdlib.h>
#include <string.h>

void af_init(AF *a) { memset(a, 0, sizeof(*a)); }
void af_free(AF *a) { free(a->syms); free(a->freqs); }

/* Symbol -> position: linear search. */
uint32_t af_find(const AF *a, uint32_t s) {
    for (uint32_t i = 0; i < a->len; i++)
        if (a->syms[i] == s) return i;
    return UINT32_MAX;
}

uint32_t af_cum(const AF *a, uint32_t idx) {
    uint32_t c = 0;
    for (uint32_t j = 0; j < idx; j++) c += a->freqs[j];
    return c;
}

/* Decoder side: walk the intervals in order until one contains target. */
uint32_t af_lower_bound(const AF *a, uint32_t target) {
    uint32_t cum = 0;
    for (uint32_t i = 0; i < a->len; i++) {
        uint32_t f = a->freqs[i];
        if (target < cum + f) return i;
        cum += f;
    }
    return a->len;
}

/* Counts floored at 1 so no symbol becomes uncodable. */
static void rescale(AF *a) {
    uint32_t nt = 0;
    for (uint32_t i = 0; i < a->len; i++) {
        uint32_t f = (a->freqs[i] + 1) >> 1;
        a->freqs[i] = f;
        nt += f;
    }
    a->total = nt;
}

void af_add(AF *a, uint32_t s, uint32_t inc) {
    uint32_t i = af_find(a, s);
    if (i != UINT32_MAX) {
        a->freqs[i] += inc;
    } else {
        if (a->len == a->cap) {
            a->cap = a->cap ? a->cap * 2 : 4;
            a->syms = (uint32_t *)realloc(a->syms, a->cap * 4);
            a->freqs = (uint32_t *)realloc(a->freqs, a->cap * 4);
        }
        a->syms[a->len] = s;
        a->freqs[a->len] = inc;
        a->len++;
    }
    a->total += inc;
    if (a->total > AF_RESCALE_LIMIT) rescale(a);
}
