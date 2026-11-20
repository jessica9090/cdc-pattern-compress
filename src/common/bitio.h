/*
 * common/bitio.h — MSB-first bit writer with Elias-gamma, and the matching
 * bit reader.
 *
 * BW and BR let a coder write and read values of any bit width, though the
 * stream is stored in whole bytes. bw_bits takes n bits and writes each
 * completed byte to the Buf (buf.h). br_bits reads bytes from the input as it
 * needs them and returns the next n bits.
 *
 * Elias-gamma (gamma' in the paper): P. Elias, "Universal codeword sets and
 * representations of the integers," IEEE Transactions on Information Theory,
 * 1975, doi: 10.1109/TIT.1975.1055349.
 */
#ifndef SEQ_COMMON_BITIO_H
#define SEQ_COMMON_BITIO_H

#include <stdint.h>
#include <stddef.h>
#include "buf.h"

/* Bit writer: accumulates bits in `acc` (nb of them pending) and appends each
 * completed byte to the Buf. */
typedef struct { Buf *buf; uint64_t acc; int nb; } BW;

void bw_init(BW *w, Buf *b);
void bw_bits(BW *w, uint64_t v, int n);     /* low n bits of v, n <= 56 */
void bw_elias_gamma(BW *w, uint64_t v);     /* v >= 1 */
void bw_flush(BW *w);                       /* pad the last byte with 0s */

/* Bit reader, the inverse of BW. Reading past the end yields zero bits and
 * sets `over`, so a truncated stream is detectable. */
typedef struct { const uint8_t *d; size_t len, pos; uint64_t acc; int nb; int over; } BR;

void     br_init(BR *r, const uint8_t *d, size_t len);
uint64_t br_bits(BR *r, int n);             /* n <= 57 */
uint64_t br_elias_gamma(BR *r);             /* v >= 1, or 0 on a corrupt code word */

#endif /* SEQ_COMMON_BITIO_H */
