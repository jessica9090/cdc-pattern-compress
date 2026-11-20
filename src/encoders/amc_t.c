/*
 * encoders/amc_t.c — AMC-T encoder/decoder. The model and what this file
 * holds are described in amc_t.h; what it shares with AMC-G, in amc_common.h.
 */
#include "amc_t.h"
#include "amc_common.h"

/* ---- Model. Its functions take the tier-3 switch t3 as a constant from one
 * of the two run_model specializations; forced inlining folds it away. ---- */
typedef struct {
    Variant cfg;
    uint32_t D;
    Tier1 t1;                /* tier 1 per jumper; D is the start context */
    AF *lm;                  /* [D]: length per target */
    AF fl;                   /* frontier-mode chain length */
    Fen f2; uint32_t *v2;    /* tier 2: v2[c] = times c was a target */
    uint32_t sum2;           /* tier 2's total weight: sum of v2 */
    uint32_t k2;             /* tier 2's misses: distinct targets so far */
    Fen f3; uint32_t sum3;   /* tier 3 (frontier only): occurrences of each non-target chunk */
    uint32_t miss3;          /* tier 3's misses: new-chunk events (frontier only) */
    uint32_t *occ;           /* [D]: occurrences of each chunk so far */
    uint32_t *first;         /* [D]: position of each chunk's first occurrence */
    uint8_t *isT;            /* [D]: 1 once a chunk has been a target */
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
    fen_init(&m->f2, D); m->v2 = xcalloc(D, 4);
    if (cfg.frontier) fen_init(&m->f3, D);      /* tier 3 exists only in frontier mode */
    m->occ = xcalloc(D, 4); m->first = xcalloc(D, 4);
    m->isT = xcalloc(D, 1);
}

static void model_free(Model *m) {
    tier1_free(&m->t1);
    for (uint32_t i = 0; i < m->D; i++) af_free(&m->lm[i]);
    af_free(&m->fl);
    free(m->lm); free(m->f2.t); free(m->v2); free(m->f3.t);
    free(m->occ); free(m->first); free(m->isT); free(m->below.v);
}

/* Chunk c occurs at position p: registers c if it is new, counts the
 * occurrence and, in frontier mode, adds it to tier 3 if c is no target. */
AMC_INLINE void see_chunk(Model *m, uint32_t c, uint32_t p, const int t3) {
    if (c == m->seen) {
        if (c >= m->D) { fprintf(stderr, "amc_t: pattern has more than D chunks\n"); exit(2); }
        m->seen++; m->first[c] = p;
    }
    m->occ[c]++;
    if (t3 && !m->isT[c]) { fen_add(&m->f3, c, 1); m->sum3++; }
}

/* Target t followed jumper j: updates j's tier 1, moves t from tier 3 into
 * tier 2 on its first landing, and counts the landing. Called before
 * see_chunk for the event's chunks. */
AMC_INLINE void note_target(Model *m, uint32_t j, uint32_t t, int was_new, const int t3) {
    tier1_add(&m->t1, j, t);
    if (!m->isT[t]) {
        /* First landing on t (a tier-2 miss): t leaves tier 3, taking all its
         * occurrences with it, and enters tier 2 with weight 1.
         * Without tier 3 a non-target is always new (t == seen), so only
         * frontier mode has an existing chunk leaving tier 3's support. */
        if (t3 && t < m->seen) {
            fen_add(&m->f3, t, -m->occ[t]); m->sum3 -= m->occ[t];
        }
        m->isT[t] = 1; m->k2++;
    }
    m->v2[t]++; fen_add(&m->f2, t, 1); m->sum2++;
    if (t3 && was_new) m->miss3++;             /* a new chunk is a tier-3 miss */
}

/* Code the target of an event with jumper j. Decoder: returns it. A return
 * of m->seen means a new chunk. The encoder's `in` tests read t, which only
 * the encoder has. */
AMC_INLINE uint32_t target_code(Codec *c, Model *m, uint32_t j, uint32_t t, const int t3) {
    /* tier 1: j's own targets */
    AF *T1 = &m->t1.tab[j];
    if (tab_code(c, T1, &t)) return t;

    /* tier 2: chunks that have been targets, by landings; escape k2 + 1.
     * Under exc, j's tier-1 symbols are excluded. */
    const AF *X = m->cfg.exc ? T1 : NULL;
    if (tier_code(c, &m->f2, m->v2, m->sum2, m->k2 + 1, X, X ? m->t1.srt[j] : NULL,
                  &m->below, !c->dec && m->isT[t], &t))
        return t;
    if (!t3) return m->seen;                   /* two tiers: the escape means new */

    /* tier 3: existing chunks that have never been a target, by occurrences;
     * escape miss3 + 1. It holds no target, so no tier-1 or tier-2 chunk is
     * in it and nothing is excluded. */
    if (tier_code(c, &m->f3, m->occ, m->sum3, m->miss3 + 1, NULL, NULL,
                  &m->below, !c->dec && t < m->seen, &t))
        return t;
    return m->seen;
}

/* The whole pattern, either direction: the encoder reads pat, the decoder
 * writes it. fr is frontier mode, which also switches tier 3 on; it is a
 * constant in each specialization below. */
AMC_INLINE void run_model_body(Codec *c, Model *m, uint32_t *pat, uint32_t N,
                               const int fr) {
    uint32_t p = 0, j = m->D, after_new = 0;
    while (p < N) {
        if (fr && after_new) {
            /* Frontier chain: r further new chunks right after a new one. */
            uint32_t r = 0;
            if (!c->dec) while (p + r < N && pat[p + r] == m->seen + r) r++;
            uint32_t v = r + 1;
            len_code(c, &m->fl, &v);
            r = v - 1;
            if (r > N - p) { fprintf(stderr, "amc_t: corrupt frontier length\n"); exit(2); }
            af_add(&m->fl, v, 1);
            /* The chain's chunks: each is the next new index (the decoder
             * writes it). Only see_chunk sees them: no tier-1, landing or
             * length update. */
            for (uint32_t i = 0; i < r; i++) {
                if (c->dec) pat[p + i] = m->seen;
                see_chunk(m, pat[p + i], p + i, fr);
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
                    fprintf(stderr, "amc_t: non-canonical pattern at %u (%u > %u)\n",
                            p, t, m->seen);
                    exit(2);
                }
            }
            t = target_code(c, m, j, t, fr);
            if (c->dec) pat[p] = t;             /* before the copy, which may read pat[p] */
            /* Existing target: copy from just after its first occurrence f.
             * The encoder measures the match; the decoder copies it, chunk by
             * chunk so that an overlapping copy reads what it just wrote. */
            int is_new = (t == m->seen);
            uint32_t l = 1;
            if (!is_new) {
                uint32_t f = m->first[t];
                if (!c->dec) while (p + l < N && pat[p + l] == pat[f + l]) l++;
                len_code(c, &m->lm[t], &l);
                if (l > N - p) { fprintf(stderr, "amc_t: corrupt match length\n"); exit(2); }
                if (c->dec) for (uint32_t k = 1; k < l; k++) pat[p + k] = pat[f + k];
            }
            note_target(m, j, t, is_new, fr);
            if (!is_new) af_add(&m->lm[t], l, 1);
            for (uint32_t k = 0; k < l; k++) see_chunk(m, pat[p + k], p + k, fr);
            /* The event's last chunk is the next jumper; after a new chunk, frontier
             * mode codes its chain next. */
            after_new = is_new;
            j = pat[p + l - 1];
            p += l;
        }
    }
}

static void run_model_3tier(Codec *c, Model *m, uint32_t *pat, uint32_t N) {
    run_model_body(c, m, pat, N, 1);
}
static void run_model_2tier(Codec *c, Model *m, uint32_t *pat, uint32_t N) {
    run_model_body(c, m, pat, N, 0);
}
static void run_model(Codec *c, Model *m, uint32_t *pat, uint32_t N) {
    if (m->cfg.frontier) run_model_3tier(c, m, pat, N);
    else                 run_model_2tier(c, m, pat, N);
}

/* ---- Entry points. ---- */
/* Encode pat into out with a fresh model. */
static void encode_to_buf(const uint32_t *pat, uint32_t N, uint32_t D, Buf *out,
                          Variant cfg) {
    ACE ace; ace_init(&ace, out);
    Codec c = { 0, &ace, NULL };
    Model m; model_init(&m, D, cfg);
    run_model(&c, &m, (uint32_t *)pat, N);     /* the encoder only reads pat */
    ace_flush(&ace);
    model_free(&m);
}

/* Encode into a scratch buffer and keep only its size. */
size_t amct_seq_encode(const uint32_t *pat, uint32_t N, uint32_t D, Variant cfg) {
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
double amct_decode_seconds(const uint32_t *pat, uint32_t N, uint32_t D,
                           Variant cfg, int *ok) {
    Buf enc; buf_init(&enc, 4096);
    encode_to_buf(pat, N, D, &enc, cfg);
    uint32_t *dec = (uint32_t *)malloc((N ? N : 1) * 4);
    /* Timed: the decode call alone, including its model setup. */
    uint64_t t0 = timer_now();
    seq_decode(enc.d, enc.len, N, D, dec, cfg);
    double sec = timer_sec(t0, timer_now());
    *ok = seq_same("AMC-T", pat, dec, N);
    free(dec); buf_free(&enc);
    return sec;
}
