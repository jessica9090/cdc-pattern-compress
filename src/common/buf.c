/*
 * common/buf.c — growable byte buffer (see buf.h).
 */
#include "buf.h"
#include <stdlib.h>

void buf_init(Buf *b, size_t cap) {
    b->d = (uint8_t *)malloc(cap);
    b->len = 0; b->cap = cap;
}
void buf_free(Buf *b) { free(b->d); }

void buf_ensure(Buf *b, size_t n) {
    if (b->len + n > b->cap) {
        while (b->len + n > b->cap) b->cap *= 2;
        b->d = (uint8_t *)realloc(b->d, b->cap);
    }
}

void buf_u8(Buf *b, uint8_t v) {
    buf_ensure(b, 1); b->d[b->len++] = v;
}
