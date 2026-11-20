/*
 * common/buf.h — growable byte buffer.
 *
 * Every coder encodes the whole pattern into a Buf. Its encode entry point
 * returns only the Buf's length to main.c, as the compressed size, and
 * its decode check (`main -rt` / `-dt`) decodes straight from the Buf's bytes.
 *
 * Allocation failures are not checked.
 */
#ifndef SEQ_COMMON_BUF_H
#define SEQ_COMMON_BUF_H

#include <stdint.h>
#include <stddef.h>

typedef struct { uint8_t *d; size_t len, cap; } Buf;   /* len bytes used of cap */

void buf_init(Buf *b, size_t cap);   /* cap > 0 */
void buf_free(Buf *b);
void buf_ensure(Buf *b, size_t n);   /* room for n more bytes (doubles cap) */
void buf_u8(Buf *b, uint8_t v);      /* append one byte */

#endif /* SEQ_COMMON_BUF_H */
