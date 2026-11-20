/*
 * encoders/mcd.c — MCD encoder/decoder (see mcd.h for the stream, the
 * matching rule and the references).
 */
#include "mcd.h"
#include "../common/buf.h"
#include "../common/bitio.h"

static int ptr_bits(uint32_t f) {
    int k = 0;
    while (((uint64_t)1 << k) < f) k++;
    return k;
}

static void encode_to_buf(const uint32_t *pat, uint32_t N, Buf *buf) {
    BW bw;
    bw_init(&bw, buf);
    uint32_t *first = (uint32_t *)malloc((N ? N : 1) * sizeof *first);
    uint32_t frontier = 0;
    size_t ci = 0;

    while (ci < N) {
        size_t start = ci;
        if (pat[ci] == frontier) {
            /* New group: the maximal run of new chunks. */
            do first[frontier++] = (uint32_t)ci++;
            while (ci < N && pat[ci] == frontier);
            bw_bits(&bw, 1, 1);
            bw_elias_gamma(&bw, ci - start);
        } else {
            /* Repeat group: extend the match from the first occurrence. */
            uint32_t first_pos = first[pat[ci]];
            size_t len = 1;
            while (ci + len < N && pat[first_pos + len] == pat[ci + len]) len++;
            ci += len;
            bw_bits(&bw, 0, 1);
            bw_elias_gamma(&bw, len);
            int pb = ptr_bits(frontier);
            if (pb) bw_bits(&bw, pat[start], pb);
        }
    }

    free(first);
    bw_flush(&bw);
}

size_t mcd_seq_encode(const uint32_t *pat, uint32_t N, uint32_t D, Variant v) {
    (void)D; (void)v;
    Buf buf;
    buf_init(&buf, 4096);
    encode_to_buf(pat, N, &buf);
    size_t bytes = buf.len;
    buf_free(&buf);
    return bytes;
}

/* Decode N indices from enc into out_pat; 0 on success. Mirror of
 * encode_to_buf; also rejects a stream that is truncated, points past the
 * frontier or overruns N. */
static int seq_decode(const uint8_t *enc, size_t enc_len, uint32_t N, uint32_t *out_pat) {
    BR br;
    br_init(&br, enc, enc_len);

    uint32_t *first = (uint32_t *)malloc((N ? N : 1) * sizeof *first);
    int rv = 1;

    uint32_t frontier = 0;
    uint32_t ci = 0;
    while (ci < N) {
        /* Read one group. */
        int is_new = (int)br_bits(&br, 1);
        uint64_t len = br_elias_gamma(&br);
        if (br.over || len == 0 || len > N - ci) goto done;
        if (is_new) {
            /* New chunks take the next indices. */
            for (uint64_t k = 0; k < len; k++) {
                first[frontier] = ci;
                out_pat[ci++] = frontier++;
            }
        } else {
            if (frontier == 0) goto done;
            uint32_t p = (uint32_t)br_bits(&br, ptr_bits(frontier));
            if (br.over || p >= frontier) goto done;
            /* Copy forward from the first occurrence; may overlap. */
            const uint32_t *src = out_pat + first[p];
            for (uint64_t k = 0; k < len; k++) out_pat[ci + k] = src[k];
            ci += (uint32_t)len;
        }
    }
    rv = 0;
done:
    free(first);
    return rv;
}

double mcd_decode_seconds(const uint32_t *pat, uint32_t N, uint32_t D,
                          Variant v, int *ok) {
    (void)D; (void)v;
    Buf enc;
    buf_init(&enc, 4096);
    encode_to_buf(pat, N, &enc);
    uint32_t *dec = (uint32_t *)calloc(N ? N : 1, 4);
    uint64_t t0 = timer_now();
    int rv = seq_decode(enc.d, enc.len, N, dec);
    double sec = timer_sec(t0, timer_now());
    int same = seq_same("MCD", pat, dec, N);
    *ok = (rv == 0) && same;
    free(dec);
    buf_free(&enc);
    return sec;
}
