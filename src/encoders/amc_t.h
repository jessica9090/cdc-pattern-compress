/*
 * encoders/amc_t.h — AMC-T: a jump-and-copy coder over a tiered jump-target
 * model, with a decoder.
 *
 * AMC = adaptive match coder; T = tier 2 is the distribution of jump
 * targets: the chunks that have been targets, weighted by how often they
 * were landed on. The parse, the table rule, tier 1, the length and frontier
 * models and the coding primitives are shared with AMC-G and described in
 * amc_common.h. This header describes the tiers below tier 1.
 *
 * Target, coded under jumper j, three tiers with frontier mode, two without:
 *   tier 1  targets seen after j       weight = times t followed j
 *   tier 2  every target seen so far   weight = times t was a target
 *   tier 3  existing chunks that have  weight = occurrences in the pattern
 *           never been a target        (frontier mode only)
 *
 * Tier 3 exists only with frontier mode, because only frontier chains bring
 * in chunks that are not targets. Tier 3 is exclusive of tiers 1 and 2 (it
 * never holds a target), so it needs no exclusion. With exc, tier 2 leaves
 * out the targets already in j's tier 1.
 *
 * The switches in Variant select the methods AMC-T[inc], AMC-T[exc],
 * AMC-T[inc]+F and AMC-T[exc]+F.
 *
 * amc_t.c holds:
 *   Model                    the coder's state; each field is described
 *                            where the struct is defined
 *   see_chunk, note_target   the model updates for each chunk and each event
 *   target_code              the tiered coding of a jump target
 *   run_model_body           the parse loop, for encoder and decoder alike;
 *                            run_model picks its two-tier or three-tier copy
 *   amct_seq_encode, amct_decode_seconds   the entry points below
 *
 * Related code: amc_common.h (the shared model and coding primitives),
 * common/model.h (AF tables), common/arithcoder.h (the arithmetic coder).
 * References: see amc_common.h.
 */
#ifndef SEQ_ENCODERS_AMC_T_H
#define SEQ_ENCODERS_AMC_T_H

#include <stdint.h>
#include "../common/seq.h"

/* Encode the pattern and return the encoded size in bytes. */
size_t amct_seq_encode(const uint32_t *pat, uint32_t N, uint32_t D, Variant v);

/* Encode untimed, return the decode's seconds; *ok = decoded == input. */
double amct_decode_seconds(const uint32_t *pat, uint32_t N, uint32_t D,
                           Variant v, int *ok);

#endif /* SEQ_ENCODERS_AMC_T_H */
