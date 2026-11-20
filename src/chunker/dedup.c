/*
 * chunker/dedup.c — bin/dedup: content-defined chunking + exact chunk
 * deduplication, producing the chunk pattern the coders in encoders/ compress.
 *
 * Usage: dedup [options] <input | ->
 *   -M BITS            boundary-mask bits: a cut is taken where the top BITS
 *                      bits of the Gear hash are zero (default 13)
 *   -m BYTES           minimum chunk size, at least 1 (default 2^BITS / 4,
 *                      or 1 at BITS = 1). Only the input's last chunk can
 *                      be shorter.
 *   -x BYTES           maximum chunk size, at least the minimum and below 2^32
 *                      (default 2^BITS * 8, at most 2^32 - 1)
 *   --dump-pattern F   write the pattern file F (format below)
 *   -q                 no statistics on stderr
 *
 * Pattern file format (read by common/io.c), little-endian:
 *   uint32_t dict_count   (D)  distinct chunks
 *   uint32_t chunk_count  (N)  chunks in the input
 *   uint32_t pattern[N]   chunk indices in [0, D), in first-appearance order
 *
 * Chunking. The rolling hash is the Gear hash,
 *     h <- (h << 1) + G[byte],    G = 256 random 64-bit constants.
 * A cut is taken before the first byte at which the top BITS bits of h are zero
 * (this file's choice of mask). Each chunk is hashed from h = 0 starting at its
 * minimum size.
 *
 * G is filled by SplitMix's generate method, with the increment GOLDEN_GAMMA,
 * from a fixed seed.
 *
 * Deduplication is exact: chunks are bucketed by a 64-bit content hash and a
 * hash match is confirmed by comparing the bytes.
 *
 * References:
 *   Pattern (indices in first-appearance order, here from 0): A. Orlitsky,
 *   N. P. Santhanam, and J. Zhang, "Universal compression of memoryless
 *   sources over unknown alphabets," IEEE Transactions on Information Theory,
 *   2004, doi: 10.1109/TIT.2004.830761.
 *   Gear hash for content-defined chunking, skip to minimum size: W. Xia
 *   et al., "FastCDC: A fast and efficient content-defined chunking approach
 *   for data deduplication," Proc. USENIX ATC, 2016,
 *   https://www.usenix.org/conference/atc16/technical-sessions/presentation/xia
 *   Default limits: 13 bits, 2K minimum and 64K maximum (Sec. 3.1):
 *   A. Muthitacharoen, B. Chen, and D. Mazières, "A low-bandwidth network
 *   file system," Proc. ACM SOSP, 2001, doi: 10.1145/502034.502052.
 *   SplitMix's generate method, with the increment GOLDEN_GAMMA and the
 *   mixing function mix64: G. L. Steele Jr., D. Lea, and C. H. Flood, "Fast
 *   splittable pseudorandom number generators," Proc. OOPSLA, 2014,
 *   doi: 10.1145/2660193.2660195.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

/* malloc/realloc that exit on failure and never request 0 bytes. */
static void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { perror("dedup: malloc"); exit(1); }
    return p;
}
static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) { perror("dedup: realloc"); exit(1); }
    return p;
}

/* ================================================================
 * Gear table generation
 * ================================================================ */
/* mix64 of Steele et al. Its Java `long` arithmetic (constants with suffix L,
 * shifts with >>>) is uint64_t arithmetic in C, hence the ULL suffix. */
static uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 33)) * 0xFF51AFD7ED558CCDULL;
    z = (z ^ (z >> 33)) * 0xC4CEB9FE1A85EC53ULL;
    return z ^ (z >> 33);
}

static uint64_t GEAR[256];

/* GEAR[i] = the i-th value returned by SplitMix's generate method, with the
 * increment GOLDEN_GAMMA. */
static void gear_init(void) {
    uint64_t s = 0x6765617248617368ULL;   /* fixed seed: "gearHash" */
    for (int i = 0; i < 256; i++) {
        s += 0x9E3779B97F4A7C15ULL;       /* GOLDEN_GAMMA */
        GEAR[i] = mix64(s);
    }
}

/* ================================================================
 * Chunker
 * ================================================================ */
typedef struct {
    uint64_t mask;   /* top `bits` bits set */
    size_t   min, max;
} CdcCfg;

/* Length of the chunk starting at p (n bytes left, n > 0). */
static size_t cdc_next(const uint8_t *p, size_t n, const CdcCfg *c) {
    size_t lim = c->max < n ? c->max : n;
    size_t i = c->min;
    uint64_t x = 0;
    /* Cut before the first byte whose hash has its masked bits zero. */
    for (; i < lim; i++) {
        x = (x << 1) + GEAR[p[i]];
        if (!(x & c->mask)) return i;
    }
    return lim;
}

/* ================================================================
 * Exact dedup table: open addressing over the distinct chunks.
 * ================================================================ */
/* 64-bit content hash: each 8-byte word is xored in and the state is passed
 * through mix64, then the length is folded in. */
static uint64_t content_hash(const uint8_t *p, size_t n) {
    uint64_t h = 0x9E3779B97F4A7C15ULL;   /* start value: GOLDEN_GAMMA again */
    size_t i = 0;
    /* Whole 8-byte words (memcpy: unaligned, host byte order). */
    for (; i + 8 <= n; i += 8) {
        uint64_t w;
        memcpy(&w, p + i, 8);
        h = mix64(h ^ w);
    }
    /* The 0-7 tail bytes as one zero-padded word, then the length. */
    uint64_t w = 0;
    for (size_t k = 0; i < n; i++, k += 8) w |= (uint64_t)p[i] << k;
    return mix64(mix64(h ^ w) ^ (uint64_t)n);
}

typedef struct {
    uint64_t *off, *hash;
    uint32_t *len;
    uint32_t  D, cap;
    uint32_t *slot;
    size_t    nslot;
} Dict;

static void dict_init(Dict *d) {
    memset(d, 0, sizeof(*d));
    d->nslot = 1024;
    d->slot = (uint32_t *)calloc(d->nslot, sizeof(uint32_t));
    if (!d->slot) { perror("dedup: calloc"); exit(1); }
}

static void dict_free(Dict *d) {
    free(d->off); free(d->hash); free(d->len); free(d->slot);
}

/* Double the slot table and reinsert every chunk by its stored hash. */
static void dict_grow(Dict *d) {
    size_t ns = d->nslot * 2;
    uint32_t *s = (uint32_t *)calloc(ns, sizeof(uint32_t));
    if (!s) { perror("dedup: calloc"); exit(1); }
    for (uint32_t k = 0; k < d->D; k++) {
        size_t j = (size_t)d->hash[k] & (ns - 1);
        while (s[j]) j = (j + 1) & (ns - 1);
        s[j] = k + 1;
    }
    free(d->slot);
    d->slot = s;
    d->nslot = ns;
}

/* Index of chunk buf[off, off+len), adding it as the next index if new. */
static uint32_t dict_index(Dict *d, const uint8_t *buf, uint64_t off, uint32_t len) {
    const uint8_t *p = buf + off;
    uint64_t h = content_hash(p, len);
    size_t j = (size_t)h & (d->nslot - 1);
    for (; d->slot[j]; j = (j + 1) & (d->nslot - 1)) {
        uint32_t k = d->slot[j] - 1;
        if (d->hash[k] == h && d->len[k] == len && memcmp(buf + d->off[k], p, len) == 0)
            return k;
    }
    if (d->D == UINT32_MAX) {
        fprintf(stderr, "dedup: more than 2^32-1 distinct chunks\n");
        exit(1);
    }
    if (d->D == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 1024;
        d->off  = (uint64_t *)xrealloc(d->off,  (size_t)d->cap * 8);
        d->hash = (uint64_t *)xrealloc(d->hash, (size_t)d->cap * 8);
        d->len  = (uint32_t *)xrealloc(d->len,  (size_t)d->cap * 4);
    }
    uint32_t k = d->D++;
    d->off[k] = off; d->hash[k] = h; d->len[k] = len;
    d->slot[j] = k + 1;
    if ((size_t)d->D * 2 > d->nslot) dict_grow(d);   /* load <= 1/2 */
    return k;
}

/* ================================================================
 * I/O
 * ================================================================ */
/* The whole of path ("-" = stdin) in one buffer, doubled as it fills. */
static uint8_t *read_all(const char *path, size_t *n_out) {
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    size_t cap = 1 << 20, n = 0;
    uint8_t *b = (uint8_t *)xmalloc(cap);
    for (;;) {
        if (n == cap) { cap *= 2; b = (uint8_t *)xrealloc(b, cap); }
        size_t r = fread(b + n, 1, cap - n, f);
        n += r;
        if (r == 0) break;
    }
    if (ferror(f)) { perror(path); exit(1); }
    if (f != stdin) fclose(f);
    *n_out = n;
    return b;
}

static void put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Write the pattern file: the 8-byte header, then the indices in 4 KiB
 * blocks. Any write or close error is fatal. */
static void write_pattern(const char *path, const uint32_t *pat, uint32_t N, uint32_t D) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    uint8_t hdr[8];
    put_u32le(hdr, D); put_u32le(hdr + 4, N);
    int ok = fwrite(hdr, 1, 8, f) == 8;
    uint8_t blk[4096];
    for (uint32_t i = 0; ok && i < N; ) {
        size_t k = 0;
        for (; k < sizeof(blk) && i < N; k += 4, i++) put_u32le(blk + k, pat[i]);
        ok = fwrite(blk, 1, k, f) == k;
    }
    if (fclose(f) != 0) ok = 0;
    if (!ok) { perror(path); exit(1); }
}

/* ================================================================
 * Driver
 * ================================================================ */
static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage: %s [-M BITS] [-m BYTES] [-x BYTES] [--dump-pattern F] [-q]\n"
        "          <input | ->\n", argv0);
    exit(1);
}

/* A non-negative decimal option value; anything else is fatal. */
static unsigned long long parse_num(const char *s, const char *what) {
    char *end;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno || end == s || *end || s[0] == '-') {
        fprintf(stderr, "dedup: bad %s '%s'\n", what, s);
        exit(1);
    }
    return v;
}

int main(int argc, char **argv) {
    const char *in = NULL, *pat_path = NULL;
    unsigned bits = 13;
    long long min = -1, max = -1;   /* -1: default from bits */
    int quiet = 0;

    /* Options, and exactly one input (a lone "-" is stdin). */
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int more = i + 1 < argc;
        if (strcmp(a, "-M") == 0 && more)                  bits = (unsigned)parse_num(argv[++i], "-M");
        else if (strcmp(a, "-m") == 0 && more)             min = (long long)parse_num(argv[++i], "-m");
        else if (strcmp(a, "-x") == 0 && more)             max = (long long)parse_num(argv[++i], "-x");
        else if (strcmp(a, "--dump-pattern") == 0 && more) pat_path = argv[++i];
        else if (strcmp(a, "-q") == 0)                     quiet = 1;
        else if (a[0] == '-' && a[1] && !in)               usage(argv[0]);
        else if (!in)                                      in = a;
        else                                               usage(argv[0]);
    }
    if (!in) usage(argv[0]);
    if (bits < 1 || bits > 32) {
        fprintf(stderr, "dedup: -M must be in [1, 32]\n");
        return 1;
    }

    /* Chunker limits: the defaults follow from BITS, explicit values are
     * checked, and every length must fit the uint32 length table. */
    if (min == 0 || max == 0) {
        fprintf(stderr, "dedup: -m and -x must be at least 1\n");
        return 1;
    }
    CdcCfg cfg;
    cfg.mask = ~0ULL << (64 - bits);
    cfg.min = min > 0 ? (size_t)min : (bits > 1 ? ((size_t)1 << bits) / 4 : 1);
    cfg.max = max > 0 ? (size_t)max : ((size_t)1 << bits) * 8;
    if (max < 0 && cfg.max > UINT32_MAX) cfg.max = UINT32_MAX;
    if (cfg.max > UINT32_MAX) {
        fprintf(stderr, "dedup: -x must be < 2^32\n");
        return 1;
    }
    if (cfg.min > cfg.max) {
        fprintf(stderr, "dedup: -m %zu exceeds -x %zu\n", cfg.min, cfg.max);
        return 1;
    }

    gear_init();
    size_t n;
    uint8_t *buf = read_all(in, &n);

    /* pat starts at n / 2^BITS + 1024 entries and grows as needed. */
    Dict d; dict_init(&d);
    size_t cap = n / ((size_t)1 << bits) + 1024, N = 0;
    uint32_t *pat = (uint32_t *)xmalloc(cap * 4);

    /* Main loop: cut the next chunk, look it up (or add it), record its index. */
    uint64_t uniq_bytes = 0;
    uint32_t dmax_len = 0, dmin_len = UINT32_MAX;
    for (size_t pos = 0; pos < n; ) {
        uint32_t len = (uint32_t)cdc_next(buf + pos, n - pos, &cfg);
        uint32_t D0 = d.D;
        uint32_t idx = dict_index(&d, buf, pos, len);
        if (d.D != D0) uniq_bytes += len;
        if (N == UINT32_MAX) { fprintf(stderr, "dedup: more than 2^32-1 chunks\n"); return 1; }
        if (N == cap) { cap *= 2; pat = (uint32_t *)xrealloc(pat, cap * 4); }
        pat[N++] = idx;
        if (len > dmax_len) dmax_len = len;
        if (len < dmin_len) dmin_len = len;
        pos += len;
    }

    if (pat_path) write_pattern(pat_path, pat, (uint32_t)N, d.D);

    /* Summary on stderr: sizes, chunk lengths and the dedup ratio. */
    if (!quiet) {
        fprintf(stderr,
            "dedup: %s  M=%u min=%zu max=%zu\n"
            "  input        %zu bytes\n"
            "  chunks N     %zu (mean %.1f B, min %u, max %u)\n"
            "  distinct D   %u (%llu bytes, N/D = %.2f)\n"
            "  dedup ratio  %.3f (input / distinct bytes)\n",
            in, bits, cfg.min, cfg.max,
            n, N, N ? (double)n / N : 0.0, N ? dmin_len : 0, dmax_len,
            d.D, (unsigned long long)uniq_bytes, d.D ? (double)N / d.D : 0.0,
            uniq_bytes ? (double)n / uniq_bytes : 0.0);
    }

    dict_free(&d);
    free(pat);
    free(buf);
    return 0;
}
