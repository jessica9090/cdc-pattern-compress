/*
 * common/io.h — pattern-file read.
 *
 * Where it fits: the input side of bin/main. bin/dedup (chunker/dedup.c)
 * writes the pattern of a chunked stream with --dump-pattern; main.c loads
 * it with read_pattern and passes the array to every coder in encoders/.
 * bench/benchmark.py parses the same 8-byte header itself to get D and N.
 *
 * Pattern file format (output of `dedup --dump-pattern`), little-endian:
 *   uint32_t dict_count   (D)
 *   uint32_t chunk_count  (N)
 *   uint32_t pattern[N]   indices in [0, D), in first-appearance order
 */
#ifndef SEQ_COMMON_IO_H
#define SEQ_COMMON_IO_H

#include <stdint.h>

/* Read a pattern file at `path`. On success returns a malloc'd array of N
 * uint32_t (caller frees) and writes the header counts to *D_out and
 * *N_out. On failure prints the reason and returns NULL. Only the file's
 * length is checked: the indices are not validated against D or against
 * first-appearance order. */
uint32_t *read_pattern(const char *path, uint32_t *D_out, uint32_t *N_out);

#endif /* SEQ_COMMON_IO_H */
