/*
 * common/arithcoder.h — arithmetic coder, adapted from Witten, Neal and
 * Cleary (1987).
 *
 * Reference:
 *   I. H. Witten, R. M. Neal, and J. G. Cleary, "Arithmetic coding for data
 *   compression," Communications of the ACM, 1987,
 *   doi: 10.1145/214762.214771.
 */
#ifndef SEQ_COMMON_ARITHCODER_H
#define SEQ_COMMON_ARITHCODER_H

#include <stdint.h>
#include <stddef.h>
#include "buf.h"
#include "bitio.h"

#define AC_BITS      32
#define AC_TOP       ((1ULL << AC_BITS) - 1)
#define AC_FIRST_QTR (AC_TOP / 4 + 1)
#define AC_HALF      (2 * AC_FIRST_QTR)
#define AC_THIRD_QTR (3 * AC_FIRST_QTR)

/* Totals must be below 2^AC_SHIFT. */
#define AC_SHIFT 24

#if AC_SHIFT > AC_BITS - 2
#error "arithcoder: AC_SHIFT exceeds AC_BITS - 2"
#endif

typedef struct {
    BW       bw;
    uint64_t low, high;
    uint64_t follow;
} ACE;

void ace_init(ACE *e, Buf *out);
/* Codes [cum, cum + freq) out of total. */
void ace_encode(ACE *e, uint32_t cum, uint32_t freq, uint32_t total);
void ace_flush(ACE *e);

typedef struct {
    BR       br;
    uint64_t low, high, value;
} ACD;

void acd_init(ACD *d, const uint8_t *src, size_t len);
/* Returns a cumulative count in [0, total), not a symbol. */
uint32_t acd_get_freq(ACD *d, uint32_t total);
void acd_advance(ACD *d, uint32_t cum, uint32_t freq, uint32_t total);

#endif /* SEQ_COMMON_ARITHCODER_H */
