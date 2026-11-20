/*
 * common/seq.c — monotonic timer and the decode check shared by every coder
 * (see seq.h).
 */
#define _POSIX_C_SOURCE 199309L

#include "seq.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

/* CLOCK_MONOTONIC, with ticks in nanoseconds. */
uint64_t timer_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
double timer_sec(uint64_t s, uint64_t e) {
    return (double)(e - s) * 1e-9;
}

int seq_same(const char *coder, const uint32_t *pat, const uint32_t *dec, uint32_t N) {
    if (memcmp(pat, dec, (size_t)N * 4) == 0) return 1;
    for (uint32_t i = 0; i < N; i++)
        if (pat[i] != dec[i]) {
            fprintf(stderr, "  %s round-trip: first mismatch at i=%u: pat=%u dec=%u\n",
                    coder, i, pat[i], dec[i]);
            break;
        }
    return 0;
}
