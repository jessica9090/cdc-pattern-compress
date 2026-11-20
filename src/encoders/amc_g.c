/*
 * encoders/amc_g.c — AMC-G encoder/decoder. The model and what this file
 * holds are described in amc_g.h; what it shares with AMC-T, in amc_common.h.
 */
#include "amc_g.h"
#include "amc_common.h"

/* ---- Model. ---- */
typedef struct {
    Variant cfg;
    uint32_t D;
    Tier1 t1;                /* tier 1 per jumper; D is the start context */
    AF *lm;                  /* [D]: length per target */
    AF fl;                   /* frontier-mode chain length */
    Fen f2; uint32_t sum2;   /* tier 2: occurrences of each existing chunk */
    uint32_t miss2;          /* tier 2's misses: new-chunk events */
    uint32_t *occ;           /* [D]: occurrences of each chunk so far (tier-2 weights) */
    uint32_t *first;         /* [D]: position of each chunk's first occurrence */
    uint32_t seen;           /* chunks seen so far; the next new chunk's index */
    Scratch below;           /* for the exclusive tier-2 decoder */
} Model;

static void model_init(Model *m, uint32_t D, Variant cfg) {
    memset(m, 0, sizeof *m);
    m->cfg = cfg; m->D = D;
    /* Every table is sized for D chunks up front and starts empty. */
    tier1_init(&m->t1, D + 1, cfg.exc);
    m->lm = xcalloc(D, sizeof(AF));
    af_init(&m->fl);
    fen_init(&m->f2, D);
    m->occ = xcalloc(D, 4); m->first = xcalloc(D, 4);
}

static void model_free(Model *m) {
    tier1_free(&m->t1);
    for (uint32_t i = 0; i < m->D; i++) af_free(&m->lm[i]);
    af_free(&m->fl);
    free(m->lm); free(m->f2.t);
    free(m->occ); free(m->first); free(m->below.v);
}

/* Chunk c occurs at position p: registers c if it is new and counts the
 * occurrence in tier 2. */
static void see_chunk(Model *m, uint32_t c, uint32_t p) {
    if (c == m->seen) {
        if (c >= m->D) { fprintf(stderr, "amc_g: pattern has more than D chunks\n"); exit(2); }
        m->seen++; m->first[c] = p;
    }
    m->occ[c]++;
    fen_add(&m->f2, c, 1); m->sum2++;
}

/* Target t followed jumper j: updates j's tier 1 and counts a tier-2 miss if
 * t was new. Called before see_chunk for the event's chunks. */
static void note_target(Model *m, uint32_t j, uint32_t t, int was_new) {
    tier1_add(&m->t1, j, t);
    if (was_new) m->miss2++;
}

/* Code the target of an event with jumper j. Decoder: returns it. A return
 * of m->seen means a new chunk. The encoder's `in` test reads t, which only
 * the encoder has. */
static uint32_t target_code(Codec *c, Model *m, uint32_t j, uint32_t t) {
    /* tier 1: j's own targets */
    AF *T1 = &m->t1.tab[j];
    if (tab_code(c, T1, &t)) return t;

    /* tier 2: every existing chunk, by occurrences; escape miss2 + 1. Under
     * exc, j's tier-1 symbols are excluded. */
    const AF *X = m->cfg.exc ? T1 : NULL;
    if (tier_code(c, &m->f2, m->occ, m->sum2, m->miss2 + 1, X, X ? m->t1.srt[j] : NULL,
                  &m->below, !c->dec && t < m->seen, &t))
        return t;
    return m->seen;
}

/* The whole pattern, either direction: the encoder reads pat, the decoder
 * writes it. The same loop as AMC-T's run_model_body. */
static void run_model(Codec *c, Model *m, uint32_t *pat, uint32_t N) {
    uint32_t p = 0, j = m->D, after_new = 0;
    while (p < N) {
        if (m->cfg.frontier && after_new) {
            /* Frontier chain: r further new chunks right after a new one. */
            uint32_t r = 0;
            if (!c->dec) while (p + r < N && pat[p + r] == m->seen + r) r++;
            uint32_t v = r + 1;
            len_code(c, &m->fl, &v);
            r = v - 1;
            if (r > N - p) { fprintf(stderr, "amc_g: corrupt frontier length\n"); exit(2); }
            af_add(&m->fl, v, 1);
            /* The chain's chunks: each is the next new index (the decoder
             * writes it). Only see_chunk sees them: no tier-1, miss or
             * length update. */
            for (uint32_t i = 0; i < r; i++) {
                if (c->dec) pat[p + i] = m->seen;
                see_chunk(m, pat[p + i], p + i);
            }
            p += r;
            if (r) j = pat[p - 1];
            after_new = 0;
        } else {
            /* Event: code the jump target, then (for an existing target) the
             * match length, then update the model. A new target's length
             * is not coded: it is always 1. */
            /* Encoder: the target is the next chunk. A pattern not in
             * first-appearance order cannot be coded. */
            uint32_t t = 0;
            if (!c->dec) {
                t = pat[p];
                if (t > m->seen) {
                    fprintf(stderr, "amc_g: non-canonical pattern at %u (%u > %u)\n",
                            p, t, m->seen);
                    exit(2);
                }
            }
            t = target_code(c, m, j, t);
            if (c->dec) pat[p] = t;              /* before the copy, which may read pat[p] */
            /* Existing target: copy from just after its first occurrence f.
             * The encoder measures the match; the decoder copies it, chunk by
             * chunk so that an overlapping copy reads what it just wrote. */
            int is_new = (t == m->seen);
            uint32_t l = 1;
            if (!is_new) {
                uint32_t f = m->first[t];
                if (!c->dec) while (p + l < N && pat[p + l] == pat[f + l]) l++;
                len_code(c, &m->lm[t], &l);
                if (l > N - p) { fprintf(stderr, "amc_g: corrupt match length\n"); exit(2); }
                if (c->dec) for (uint32_t k = 1; k < l; k++) pat[p + k] = pat[f + k];
            }
            note_target(m, j, t, is_new);
            if (!is_new) af_add(&m->lm[t], l, 1);
            for (uint32_t k = 0; k < l; k++) see_chunk(m, pat[p + k], p + k);
            /* The event's last chunk is the next jumper; after a new chunk, frontier
             * mode codes its chain next. */
            after_new = is_new;
            j = pat[p + l - 1];
            p += l;
        }
    }
}

/* ---- Entry points. ---- */
/* Encode pat into out with a fresh model. */
static void encode_to_buf(const uint32_t *pat, uint32_t N, uint32_t D, Buf *out,
                          Variant cfg) {
    ACE ace; ace_init(&ace, out);
    Codec c = { 0, &ace, NULL };
    Model m; model_init(&m, D, cfg);
    run_model(&c, &m, (uint32_t *)pat, N);      /* the encoder only reads pat */
    ace_flush(&ace);
    model_free(&m);
}

/* Encode into a scratch buffer and keep only its size. */
size_t amcg_seq_encode(const uint32_t *pat, uint32_t N, uint32_t D, Variant cfg) {
    Buf buf; buf_init(&buf, 4096);
    encode_to_buf(pat, N, D, &buf, cfg);
    size_t bytes = buf.len;
    buf_free(&buf);
    return bytes;
}

/* Decode N indices into out_pat. */
static void seq_decode(const uint8_t *enc, size_t enc_len, uint32_t N,
                       uint32_t D, uint32_t *out_pat, Variant cfg) {
    ACD acd; acd_init(&acd, enc, enc_len);
    Codec c = { 1, NULL, &acd };
    Model m; model_init(&m, D, cfg);
    run_model(&c, &m, out_pat, N);
    model_free(&m);
}

/* Encode (untimed), then time only the decode and compare with the input. */
double amcg_decode_seconds(const uint32_t *pat, uint32_t N, uint32_t D,
                           Variant cfg, int *ok) {
    Buf enc; buf_init(&enc, 4096);
    encode_to_buf(pat, N, D, &enc, cfg);
    uint32_t *dec = (uint32_t *)malloc((N ? N : 1) * 4);
    /* Timed: the decode call alone, including its model setup. */
    uint64_t t0 = timer_now();
    seq_decode(enc.d, enc.len, N, D, dec, cfg);
    double sec = timer_sec(t0, timer_now());
    *ok = seq_same("AMC-G", pat, dec, N);
    free(dec); buf_free(&enc);
    return sec;
}
