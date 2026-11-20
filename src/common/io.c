/*
 * common/io.c — pattern-file read (see io.h for the file format).
 */
#include "io.h"
#include <stdio.h>
#include <stdlib.h>

uint32_t *read_pattern(const char *path, uint32_t *D_out, uint32_t *N_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    uint32_t D, N;
    uint32_t *pat = NULL;
    /* Header, then N indices, read straight into host integers: this matches
     * the little-endian file only on a little-endian host. Any short read
     * (or a failed allocation) rejects the file. */
    if (fread(&D, 4, 1, f) != 1 || fread(&N, 4, 1, f) != 1
        || !(pat = (uint32_t *)malloc(N ? (size_t)N * 4 : 1))
        || fread(pat, 4, N, f) != N) {
        fprintf(stderr, "%s: truncated or unreadable pattern file\n", path);
        free(pat); fclose(f);
        return NULL;
    }
    fclose(f);
    *D_out = D;
    *N_out = N;
    return pat;
}
