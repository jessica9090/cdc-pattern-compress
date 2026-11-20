/*
 * encoders/mcd.h — MCD: multi-chunk deduplication of the pattern, and its decoder.
 *
 * Stream: the pattern is split into groups, each one of two kinds. With f the
 * frontier (the number of distinct chunks seen so far):
 *   new group     flag 1, then Elias-gamma(V): the V >= 1 consecutive new
 *                 chunks f, f+1, ..., f+V-1, V maximal.
 *   repeat group  flag 0, then Elias-gamma(W), then a pointer: the repeated
 *                 chunk's index pat[c] in [0, f), in ceil(log2 f) bits. With
 *                 c~ = first[pat[c]], the position of its first occurrence,
 *                 W is maximal such that pat[c~+k] == pat[c+k] for
 *                 0 <= k < W.
 *
 * Choices made in this coder:
 *   - Only the pattern is coded: new groups write no chunk contents, which
 *     are not part of the input or output. The pattern is taken as given
 *     from the pattern file.
 *   - No total length is written. The decoder is given the stream's byte length
 *     and N (the chunk_count from the pattern file; see main.c), as for every
 *     coder, so N is not coded and not counted in the size.
 *   - The pointer is written in a fixed ceil(log2 f) bits.
 *   - Elias-gamma codes the group lengths V and W.
 *
 * Implementation note: first[] maps each chunk index to the position of that
 * chunk's first occurrence, where a repeat group with that pointer copies
 * from. It is allocated once with N entries before encoding or decoding
 * starts, since there are at most N distinct chunks.
 *
 * References:
 *   Multi-chunk deduplication: U. Niesen, "An information-theoretic analysis of
 *   deduplication," IEEE Transactions on Information Theory, 2019,
 *   doi: 10.1109/TIT.2019.2916037.
 *   Elias-gamma (gamma' in the paper): P. Elias, "Universal codeword sets and
 *   representations of the integers," IEEE Transactions on Information Theory,
 *   1975, doi: 10.1109/TIT.1975.1055349.
 */
#ifndef SEQ_ENCODERS_MCD_H
#define SEQ_ENCODERS_MCD_H

#include <stdint.h>
#include "../common/seq.h"

/* Encode the pattern and return the encoded size in bytes. */
size_t mcd_seq_encode(const uint32_t *pat, uint32_t N, uint32_t D, Variant v);

/* Encode untimed, return the decode's seconds; *ok = decoded == input. */
double mcd_decode_seconds(const uint32_t *pat, uint32_t N, uint32_t D,
                          Variant v, int *ok);

#endif /* SEQ_ENCODERS_MCD_H */
