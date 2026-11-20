/*
 * common/seq.h — definitions shared by every coder and by main.c.
 *
 * Where it fits: the interface between bin/main (main.c) and the coders.
 * Every coder exposes the same two entry points, which is how main.c's
 * CODERS table calls them all the same way:
 *     <coder>_seq_encode      (pat, N, D, variant)      -> encoded bytes
 *     <coder>_decode_seconds  (pat, N, D, variant, *ok) -> seconds
 * The encode returns only the size of its output, which main.c prints as the
 * C_seq(B) column. main.c times each encode with the timer here, and each
 * coder's *_decode_seconds times its decode with it.
 */
#ifndef SEQ_COMMON_SEQ_H
#define SEQ_COMMON_SEQ_H

#include <stdint.h>
#include <stdlib.h>

/* Monotonic timer: timer_now returns ticks, timer_sec converts interval to seconds. */
uint64_t timer_now(void);
double   timer_sec(uint64_t s, uint64_t e);

/* The two switches of the AMC coders (see encoders/amc_common.h). */
typedef struct {
    int exc;       /* tier 2 excludes the jumper's tier-1 targets */
    int frontier;  /* frontier mode on */
} Variant;

/* 1 if dec equals pat over N indices. Otherwise 0, after printing the first
 * differing position to stderr under the coder's name. */
int seq_same(const char *coder, const uint32_t *pat, const uint32_t *dec, uint32_t N);

#endif /* SEQ_COMMON_SEQ_H */
