/*
 * encoders/amc_common.h — the model and code shared by AMC-T and AMC-G.
 *
 * Included by amc_t.c and amc_g.c (entry points in amc_t.h and amc_g.h). The
 * coders differ only in their tiers below tier 1, which each coder's header
 * describes. Related code:
 *   common/model.h, model.c            AF tables: tier 1, lengths, frontier count
 *   common/arithcoder.h, arithcoder.c  the arithmetic coder behind every decision
 *   common/seq.h                       Variant (the two switches), the timer
 *
 * AMC = adaptive match coder. Two switches (Variant) give the four methods
 * [inc], [exc], [inc]+F and [exc]+F:
 *   exc       tier 2 excludes the jumper's tier-1 targets (PPM exclusion)
 *   frontier  after a new chunk, the run of further new chunks is coded as
 *             one length, not one jump per chunk
 *
 * The pattern is parsed into events. Each starts at a target t and is coded
 * in context j, the chunk just before it:
 *   t existing  the longest match starting from t's first occurrence.
 *   t new       with frontier, the count r >= 0 of further new chunks
 *               t+1, ..., t+r, coded as r + 1; without, the event is t alone.
 * The parse loop is in each coder's .c (run_model_body in amc_t.c, run_model
 * in amc_g.c).
 *
 * Table rule: a seen symbol weighs its count, and the escape weighs
 * misses + 1, a miss being an escape already coded from that table. An empty
 * table escapes with probability 1.
 *
 * Tier 1: one AF table per jumper j (plus one for the first event) of the
 * targets that followed j. Its escape says that t has not followed j before,
 * and t is then coded in the lower tiers. Every escaped t joins j's table, so
 * the table's misses are its distinct targets, and the escape weighs their
 * number + 1.
 *
 * Lower tiers: Fenwick trees over chunk indices, shared by all jumpers. An
 * escape from the last tier means t is new.
 *
 * Lengths: one AF table per target for its match lengths, Elias-gamma on an
 * escape. Frontier adds one global table for r + 1.
 *
 * No total length is coded: the decoder is given N (chunk_count from the
 * pattern file; see main.c) and stops after N indices.
 *
 * References, for both AMC coders:
 *   Escapes and exclusion (exc): J. G. Cleary and I. H. Witten, "Data
 *   compression using adaptive coding and partial string matching," IEEE
 *   Transactions on Communications, 1984, doi: 10.1109/TCOM.1984.1096090.
 *
 *   Escape weight = number of distinct symbols (method C; the + 1 is added
 *   here): A. Moffat, "Implementing the PPM data compression scheme," IEEE
 *   Transactions on Communications, 1990, doi: 10.1109/26.61469.
 *
 *   Parse into copies from a chunk's first occurrence and runs of new
 *   chunks (multi-chunk deduplication): U. Niesen, "An information-theoretic
 *   analysis of deduplication," IEEE Transactions on Information Theory,
 *   2019, doi: 10.1109/TIT.2019.2916037.
 *
 *   Fenwick tree (fen_add, fen_prefix): P. M. Fenwick, "A new data structure
 *   for cumulative frequency tables," Software: Practice and Experience,
 *   1994, doi: 10.1002/spe.4380240306.
 *
 *   Frequency-to-symbol search (fen_find): P. M. Fenwick, "A new data structure
 *   for cumulative probability tables: an improved frequency-to-symbol algorithm,"
 *   University of California, Santa Cruz, Tech. Rep. UCSC-CRL-95-17, 1995,
 *   https://tr.soe.ucsc.edu/research/technical-reports/UCSC-CRL-95-17
 *
 *   Elias-gamma (gamma' in the paper): P. Elias, "Universal codeword sets
 *   and representations of the integers," IEEE Transactions on Information
 *   Theory, 1975, doi: 10.1109/TIT.1975.1055349.
 *
 *   AF tables: see common/model.h. Arithmetic coder: see common/arithcoder.h.
 */
#ifndef SEQ_ENCODERS_AMC_COMMON_H
#define SEQ_ENCODERS_AMC_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/buf.h"
#include "../common/arithcoder.h"
#include "../common/model.h"
#include "../common/seq.h"

/* AF tables must rescale before their total reaches the coder's bound. Then
 * tab_code's total (counts plus escape len + 1) stays below 2^AC_SHIFT for
 * any table with fewer than 2^22 distinct symbols. */
#if AF_RESCALE_LIMIT >= (1u << AC_SHIFT)
#error "amc: AF_RESCALE_LIMIT must be below 2^AC_SHIFT"
#endif

/* Forced inlining, so a model switch passed as a constant folds away. */
#if defined(__GNUC__) || defined(__clang__)
#define AMC_INLINE static inline __attribute__((always_inline))
#else
#define AMC_INLINE static inline
#endif

/* calloc that exits on failure and never requests 0 bytes. */
static inline void *xcalloc(size_t n, size_t s) {
    void *p = calloc(n ? n : 1, s);
    if (!p) { perror("amc: calloc"); exit(1); }
    return p;
}

/* ---- One object for both directions, so each model is written once. ---- */
typedef struct { int dec; ACE *e; ACD *d; } Codec;

/* Arithmetic-coder primitives. put (encoder) codes [cum, cum + freq) of total;
 * peek returns the decoder's point in [0, total), and take consumes the
 * interval holding it. check_total enforces 0 < total < 2^AC_SHIFT. */
static inline void check_total(uint32_t total) {
    if (total == 0 || total >= (1u << AC_SHIFT)) {
        fprintf(stderr, "amc: arithmetic-coder total %u out of range\n", total);
        exit(2);
    }
}
static inline void put(Codec *c, uint32_t cum, uint32_t freq, uint32_t total) {
    check_total(total);
    ace_encode(c->e, cum, freq, total);
}
static inline uint32_t peek(Codec *c, uint32_t total) {
    check_total(total);
    return acd_get_freq(c->d, total);
}
static inline void take(Codec *c, uint32_t cum, uint32_t freq, uint32_t total) {
    acd_advance(c->d, cum, freq, total);
}

/* A fair bit (total 2): the encoder codes *b, the decoder sets it. */
static inline void bit_code(Codec *c, uint32_t *b) {
    if (!c->dec) { put(c, *b, 1, 2); return; }
    uint32_t t = peek(c, 2);
    take(c, t, 1, 2);
    *b = t;
}

/* Elias-gamma of v >= 1, one binary decision at a time. */
static inline void gamma_code(Codec *c, uint32_t *v) {
    /* Encoder */
    if (!c->dec) {
        int n = 0;
        for (uint32_t t = *v; t > 1; t >>= 1) n++;
        uint32_t z = 0, o = 1;
        for (int i = 0; i < n; i++) bit_code(c, &z);
        bit_code(c, &o);
        for (int i = n - 1; i >= 0; i--) { uint32_t b = (*v >> i) & 1; bit_code(c, &b); }
        return;
    }
    /* Decoder: an encoder's stream has at most 31 zeros here; the cap only
     * ends the loop on a bad stream, where the decoder can return zeros forever. */
    int n = 0;
    for (;;) {
        uint32_t b; bit_code(c, &b);
        if (b) break;
        if (++n > 31) { fprintf(stderr, "amc: corrupt Elias-gamma code\n"); exit(2); }
    }
    uint32_t v1 = 1;
    for (int i = 0; i < n; i++) { uint32_t b; bit_code(c, &b); v1 = (v1 << 1) | b; }
    *v = v1;
}

/* Code s against an AF table: weight = count, escape = len + 1. Returns 1 on
 * a hit (decoder: *s set), 0 on escape. Does not update the table. */
static inline int tab_code(Codec *c, AF *a, uint32_t *s) {
    uint32_t esc = a->len + 1, total = a->total + esc;
    /* Encoder: s's interval, or the escape if s is absent. */
    if (!c->dec) {
        uint32_t i = af_find(a, *s);
        if (i == UINT32_MAX) { put(c, a->total, esc, total); return 0; }
        put(c, af_cum(a, i), a->freqs[i], total);
        return 1;
    }
    /* Decoder: a point at or past the symbols' mass is the escape. */
    uint32_t x = peek(c, total);
    if (x >= a->total) { take(c, a->total, esc, total); return 0; }
    uint32_t i = af_lower_bound(a, x);
    take(c, af_cum(a, i), a->freqs[i], total);
    *s = a->syms[i];
    return 1;
}

/* A value >= 1 (match length or frontier count) from table a, else an escape
 * then Elias-gamma. The caller adds it to a afterwards. */
static inline void len_code(Codec *c, AF *a, uint32_t *l) {
    if (!tab_code(c, a, l)) gamma_code(c, l);
}

/* ---- Tier 1: one AF table per jumper; under exc each support is also kept
 * sorted, for tier_code's exclusive decoder. ---- */
typedef struct {
    uint32_t n;               /* tables: D + 1, index D is the start context */
    AF *tab;                  /* [n]: one per jumper, D = distinct chunks */
    uint32_t **srt, *srtcap;  /* [n]: sorted supports (exc only, else NULL) */
} Tier1;

static inline void tier1_init(Tier1 *t1, uint32_t n, int exc) {
    t1->n = n;
    t1->tab = xcalloc(n, sizeof(AF));
    t1->srt = exc ? xcalloc(n, sizeof(uint32_t *)) : NULL;
    t1->srtcap = exc ? xcalloc(n, 4) : NULL;
}
static inline void tier1_free(Tier1 *t1) {
    for (uint32_t i = 0; i < t1->n; i++) {
        af_free(&t1->tab[i]);
        if (t1->srt) free(t1->srt[i]);
    }
    free(t1->tab); free(t1->srt); free(t1->srtcap);
}
/* Target t followed jumper j: one more t in j's table. */
static inline void tier1_add(Tier1 *t1, uint32_t j, uint32_t t) {
    AF *a = &t1->tab[j];
    uint32_t old_len = a->len;
    af_add(a, t, 1);
    if (!t1->srt || a->len == old_len) return;
    /* New symbol under exc: insert it into j's sorted support. */
    if (a->len > t1->srtcap[j]) {
        t1->srtcap[j] = t1->srtcap[j] ? 2 * t1->srtcap[j] : 4;
        t1->srt[j] = realloc(t1->srt[j], t1->srtcap[j] * 4);
    }
    uint32_t *s = t1->srt[j], i = old_len;
    while (i > 0 && s[i - 1] > t) { s[i] = s[i - 1]; i--; }
    s[i] = t;
}

/* ---- Fenwick tree over chunk indices: O(log D) point add and prefix sum,
 * for the lower tiers' cumulative weights. ---- */
typedef struct { uint32_t n, top; uint32_t *t; } Fen;

static inline void fen_init(Fen *f, uint32_t n) {
    f->n = n; f->t = xcalloc(n + 1, 4);
    f->top = 1; while (f->top * 2 <= n) f->top *= 2;
}
static inline void fen_add(Fen *f, uint32_t i, uint32_t delta) {   /* delta mod 2^32 */
    for (i++; i <= f->n; i += i & -i) f->t[i] += delta;
}
static inline uint32_t fen_prefix(const Fen *f, uint32_t i) {      /* sum of [0, i) */
    uint32_t s = 0;
    for (; i > 0; i -= i & -i) s += f->t[i];
    return s;
}
/* Largest i with prefix(i) <= x: for x < total, the symbol holding x. pos
 * ends at the 1-based slot of the last entry ending at or before x, which is
 * the 0-based index of the entry after it, the one holding x. */
static inline uint32_t fen_find(const Fen *f, uint32_t x) {
    uint32_t pos = 0;
    for (uint32_t step = f->top; step; step >>= 1)
        if (pos + step <= f->n && f->t[pos + step] <= x) { pos += step; x -= f->t[pos]; }
    return pos;
}

/* Growable scratch array for the exclusive decoder in tier_code. */
typedef struct { uint32_t *v, cap; } Scratch;

/* Code *t against a lower tier: Fenwick tree f with chunk weights w[c], total
 * sum and escape weight esc. X (NULL = none) is an AF table whose symbols are
 * excluded, xs its support sorted: their mass is removed, the escape weight
 * kept. Encoder: `in` says whether *t is in the tier. Returns 1 on a hit
 * (decoder: *t set), 0 on escape. Does not update the tier. */
AMC_INLINE int tier_code(Codec *c, const Fen *f, const uint32_t *w, uint32_t sum,
                         uint32_t esc, const AF *X, const uint32_t *xs,
                         Scratch *below, int in, uint32_t *t) {
    int ex = X && X->len;
    if (ex) for (uint32_t i = 0; i < X->len; i++) sum -= w[X->syms[i]];
    uint32_t total = sum + esc;
    if (!c->dec) {
        if (!in) { put(c, sum, esc, total); return 0; }
        /* Encoder: cum = tree prefix below t, minus the excluded mass below t. */
        uint32_t cum = fen_prefix(f, *t);
        if (ex) for (uint32_t i = 0; i < X->len; i++)
            if (X->syms[i] < *t) cum -= w[X->syms[i]];
        put(c, cum, w[*t], total);
        return 1;
    }
    /* Decoder: a point below sum lies in some chunk's interval. */
    uint32_t x = peek(c, total);
    if (x >= sum) { take(c, sum, esc, total); return 0; }
    /* Exclusive: with excluded symbols e_0 < e_1 < ... and below[i] = sum of
     * w[e_m] for m < i, the symbol holding x has i excluded symbols beneath
     * it, i = the number of e_m with prefix(e_m) - below[m] <= x. Its point
     * in the full tree is x + below[i]. */
    uint32_t below_i = 0;
    if (ex) {
        uint32_t k = X->len;
        if (k + 1 > below->cap) {
            below->cap = 2 * (k + 1);
            below->v = realloc(below->v, below->cap * 4);
        }
        below->v[0] = 0;
        for (uint32_t i = 0; i < k; i++) below->v[i + 1] = below->v[i] + w[xs[i]];
        /* Binary search for i over the sorted support. */
        uint32_t lo = 0, hi = k;
        while (lo < hi) {
            uint32_t mid = (lo + hi) / 2;
            if (fen_prefix(f, xs[mid]) - below->v[mid] <= x) lo = mid + 1;
            else hi = mid;
        }
        below_i = below->v[lo];
    }
    /* Map back into the full tree and consume t's reduced interval. */
    *t = fen_find(f, x + below_i);
    take(c, fen_prefix(f, *t) - below_i, w[*t], total);
    return 1;
}

#endif /* SEQ_ENCODERS_AMC_COMMON_H */
