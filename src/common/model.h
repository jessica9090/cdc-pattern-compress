/*
 * common/model.h — adaptive frequency table AF.
 *
 * Where it fits: the probability model under AMC-T and AMC-G (encoders/
 * amc_t.h, amc_g.h). Their tier-1, match-length and frontier-count decisions
 * are lookups in an AF table: tab_code and len_code (encoders/amc_common.h)
 * pass the table's interval to the arithmetic coder (arithcoder.h).
 *
 * Every query is a linear scan: the simplest structure, meant for small
 * tables (tier 1 per jumper, match length per target, frontier chain length).
 *
 * References:
 *   Adaptive frequency model, counts halved when the total reaches a
 *   ceiling: I. H. Witten, R. M. Neal, and J. G. Cleary, "Arithmetic coding
 *   for data compression," Communications of the ACM, 1987,
 *   doi: 10.1145/214762.214771.
 */
#ifndef SEQ_COMMON_MODEL_H
#define SEQ_COMMON_MODEL_H

#include <stdint.h>

/* Rescale ceiling. Once a table's total passes it, af_add halves its counts.
 * It sits below the arithmetic coder's bound 2^AC_SHIFT (arithcoder.h),
 * leaving room for the escape weight that encoders/amc_common.h adds to the
 * total when it codes against the table. */
#define AF_RESCALE_LIMIT (1u << 23)

typedef struct {
    uint32_t *syms, *freqs;    /* parallel arrays, in insertion order */
    uint32_t len, cap, total;  /* len = distinct symbols; total = sum of freqs */
} AF;

void     af_init(AF *a);
void     af_free(AF *a);

/* Position of symbol s, or UINT32_MAX if absent. */
uint32_t af_find(const AF *a, uint32_t s);

/* Add inc to s's count (appending s if new). */
void     af_add(AF *a, uint32_t s, uint32_t inc);

/* Start of position idx's interval: the sum of freqs[0..idx). */
uint32_t af_cum(const AF *a, uint32_t idx);

/* Position whose interval [cum, cum + freq) holds target (target < total). */
uint32_t af_lower_bound(const AF *a, uint32_t target);

#endif /* SEQ_COMMON_MODEL_H */
