/*
 * encoders/amc_g.h — AMC-G: a jump-and-copy coder over a two-tier
 * jump-target model, with a decoder.
 *
 * AMC = adaptive match coder; G = tier 2 is global: every existing chunk,
 * weighted by its occurrences in the pattern. The parse, the table rule,
 * tier 1, the length and frontier models and the coding primitives are
 * shared with AMC-T and described in amc_common.h.
 *
 * Target, coded under jumper j, always two tiers:
 *   tier 1  targets seen after j       weight = times t followed j
 *   tier 2  every existing chunk       weight = occurrences in the pattern
 * Tier 2's misses are the new-chunk events (in frontier mode, a chain chunk
 * joins tier 2 but does not count as a new-chunk event). With exc, tier 2
 * leaves out the targets already in j's tier 1.
 *
 * The switches in Variant select the methods AMC-G[inc], AMC-G[exc],
 * AMC-G[inc]+F and AMC-G[exc]+F.
 *
 * amc_g.c holds:
 *   Model                    the coder's state; each field is described
 *                            where the struct is defined
 *   see_chunk, note_target   the model updates for each chunk and each event
 *   target_code              the two-tier coding of a jump target
 *   run_model                the parse loop, for encoder and decoder alike
 *   amcg_seq_encode, amcg_decode_seconds   the entry points below
 *
 * Related code: amc_common.h (the shared model and coding primitives),
 * common/model.h (AF tables), common/arithcoder.h (the arithmetic coder).
 * References: see amc_common.h.
 */
#ifndef SEQ_ENCODERS_AMC_G_H
#define SEQ_ENCODERS_AMC_G_H

#include <stdint.h>
#include "../common/seq.h"

/* Encode the pattern and return the encoded size in bytes. */
size_t amcg_seq_encode(const uint32_t *pat, uint32_t N, uint32_t D, Variant v);

/* Encode untimed, return the decode's seconds; *ok = decoded == input. */
double amcg_decode_seconds(const uint32_t *pat, uint32_t N, uint32_t D,
                           Variant v, int *ok);

#endif /* SEQ_ENCODERS_AMC_G_H */
