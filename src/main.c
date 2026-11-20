/*
 * main.c — bin/main: the shared benchmark driver for the pattern coders in
 * encoders/ (MCD, AMC-T, AMC-G).
 *
 * Reads a pattern file (output of `dedup --dump-pattern`; format in
 * common/io.h) and runs the registered coders on it.
 *
 * Usage: main <pattern_file> [-m METHOD] [-rt | -dt | -H]
 *   (default)   encode with every coder (or just -m METHOD) and print one row
 *               "METHOD  C_seq(B)  bits/chk  time_ms" per coder; without
 *               -m the rows follow the pattern's entropy line
 *   -m METHOD   run only the named coder
 *   -rt         round-trip self-test: encode, decode, compare; one line
 *               "METHOD round-trip on ...: OK|FAIL"
 *   -dt         decode wall clock: encode untimed, time the decode, check it;
 *               one line "DEC METHOD <ms> OK|FAIL"
 *   -H          print only the entropy line
 *
 * Timing: time_ms is the wall clock of the whole encoder call, measured here
 * rather than inside each coder.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "common/seq.h"
#include "common/io.h"

#include "encoders/mcd.h"
#include "encoders/amc_t.h"
#include "encoders/amc_g.h"

/* ---- Coder registry: the names accepted by -m, in output order. ---- */

/* Encode: (pat, N, D, variant) -> encoded size in bytes. */
typedef size_t (*enc_fn)(const uint32_t *, uint32_t, uint32_t, Variant);
/* Decode check: encode untimed, return the decode's seconds, *ok = match.
 * -dt reports the time; -rt is the same check with the time ignored. */
typedef double (*dec_fn)(const uint32_t *, uint32_t, uint32_t, Variant, int *);

typedef struct {
    const char *name;
    enc_fn  enc;
    dec_fn  dec;
    Variant var;   /* {exc, frontier}; ignored by MCD */
} Coder;

static const Coder CODERS[] = {
    {"MCD",          mcd_seq_encode,  mcd_decode_seconds,  {0, 0}},
    {"AMC-T[inc]",   amct_seq_encode, amct_decode_seconds, {0, 0}},
    {"AMC-T[exc]",   amct_seq_encode, amct_decode_seconds, {1, 0}},
    {"AMC-T[inc]+F", amct_seq_encode, amct_decode_seconds, {0, 1}},
    {"AMC-T[exc]+F", amct_seq_encode, amct_decode_seconds, {1, 1}},
    {"AMC-G[inc]",   amcg_seq_encode, amcg_decode_seconds, {0, 0}},
    {"AMC-G[exc]",   amcg_seq_encode, amcg_decode_seconds, {1, 0}},
    {"AMC-G[inc]+F", amcg_seq_encode, amcg_decode_seconds, {0, 1}},
    {"AMC-G[exc]+F", amcg_seq_encode, amcg_decode_seconds, {1, 1}},
};
#define N_CODERS (sizeof(CODERS) / sizeof(CODERS[0]))

/* Order-0 empirical entropy H0 of the pattern, in bits per chunk: the
 * entropy of the index frequencies over the whole pattern. H0_total is
 * H0 * N / 8 bytes. */
static void print_entropy(const uint32_t *pat, uint32_t N, uint32_t D) {
    uint32_t *freq = (uint32_t *)calloc(D ? D : 1, 4);
    for (uint32_t i = 0; i < N; i++) freq[pat[i]]++;
    double H0 = 0;
    for (uint32_t i = 0; i < D; i++)
        if (freq[i]) { double p = (double)freq[i] / N; H0 -= p * log2(p); }
    free(freq);
    printf("Chunk entropy: H0=%.4f bits/chunk, H0_total=%.0f bytes\n",
           H0, H0 * N / 8.0);
}

int main(int argc, char **argv) {
    int rt_only = 0, dt_only = 0, ent_only = 0;
    const char *path = NULL, *only = NULL;

    /* Parse arguments; any non-flag argument is the pattern file. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-rt") == 0) rt_only = 1;
        else if (strcmp(argv[i], "-dt") == 0) dt_only = 1;
        else if (strcmp(argv[i], "-H") == 0) ent_only = 1;
        else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) only = argv[++i];
        else path = argv[i];
    }
    if (!path) {
        fprintf(stderr, "Usage: %s <pattern_file> [-m METHOD] [-rt | -dt | -H]\n", argv[0]);
        return 1;
    }
    /* -m must name a registered coder; list them if it does not. */
    if (only) {
        size_t m = 0;
        while (m < N_CODERS && strcmp(only, CODERS[m].name) != 0) m++;
        if (m == N_CODERS) {
            fprintf(stderr, "Unknown method '%s'. Known:", only);
            for (m = 0; m < N_CODERS; m++) fprintf(stderr, " %s", CODERS[m].name);
            fprintf(stderr, "\n");
            return 1;
        }
    }

    /* Load the pattern (not included in any timing). */
    uint32_t D, N;
    uint32_t *pat = read_pattern(path, &D, &N);
    if (!pat) return 1;

    /* -H: entropy line only. */
    if (ent_only) {
        print_entropy(pat, N, D);
        free(pat);
        return 0;
    }

    /* -rt / -dt: decode checks only, no size table. -rt wins if both are
     * given. Exit status 1 if any check fails. */
    if (rt_only || dt_only) {
        int fail = 0;
        for (size_t m = 0; m < N_CODERS; m++) {
            const Coder *c = &CODERS[m];
            if (only && strcmp(only, c->name) != 0) continue;
            int ok = 0;
            double sec = c->dec(pat, N, D, c->var, &ok);
            if (rt_only)
                printf("%s round-trip on %s: %s (D=%u, N=%u)\n",
                       c->name, path, ok ? "OK" : "FAIL", D, N);
            else
                printf("DEC %s %.3f %s\n", c->name, sec * 1000.0, ok ? "OK" : "FAIL");
            fail |= !ok;
        }
        free(pat);
        return fail;
    }

    /* Default: entropy line, then one size/time row per coder. */
    printf("Pattern: %s  D=%u  N=%u\n\n", path, D, N);
    if (!only) { print_entropy(pat, N, D); printf("\n"); }
    printf("%-12s  %10s  %8s  %8s\n", "Method", "C_seq(B)", "bits/chk", "time_ms");
    printf("------------------------------------------------\n");

    /* Time each whole encoder call. */
    for (size_t m = 0; m < N_CODERS; m++) {
        const Coder *c = &CODERS[m];
        if (only && strcmp(only, c->name) != 0) continue;
        uint64_t t0 = timer_now();
        size_t bytes = c->enc(pat, N, D, c->var);
        double sec = timer_sec(t0, timer_now());
        printf("%-12s  %10zu  %8.3f  %8.3f\n",
               c->name, bytes, 8.0 * bytes / N, sec * 1000);
    }

    free(pat);
    return 0;
}
