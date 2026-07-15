/*  TCAM2 - Tovy Compresor de Audio MP2
    Domain-aware lossless codec for unpackmp2 um2 format.

    Exploits um2 structure: parses fields (headers, bit allocations,
    scalefactors, samples, filler) and encodes each with a model
    optimized for its statistical properties.

    Target: 50-100x faster than lpaq8 at comparable compression ratio.

    Copyright (C) 2026 Tovy + Michael Henke (unpackmp2)
    GPLv3.
*/

#ifndef TCAM2_H
#define TCAM2_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- limits ---- */
#define TCAM2_MAGIC   0x324D4143  /* "CAM2" in little-endian */
#define TCAM2_VERSION 1

/* ---- range coder (byte-oriented, 32-bit) ---- */
typedef struct {
    uint32_t low;       /* lower bound of current range */
    uint32_t range;     /* current range width */
    uint32_t pending;   /* pending carry bytes */
    FILE    *fp;        /* I/O stream */
    int      dec;       /* 1 = decoder, 0 = encoder */
    int      eof;       /* decoder EOF flag */
} RC;

/* ---- adaptive byte model (order-0 with decay) ---- */
#define ADAPT_N_SYMBOLS  256
typedef struct {
    unsigned short count[ADAPT_N_SYMBOLS];
    unsigned short total;
} AdaptModel;

/* ---- context model (order-1: prev byte -> current byte) ---- */
#define CTX_N_CONTEXTS   256
typedef struct {
    AdaptModel ctx[CTX_N_CONTEXTS];
} CtxModel;

/* ---- field types for debug/analysis ---- */
typedef enum {
    FIELD_FRAMES_IN_BLOCK = 0,
    FIELD_FRAME_HEADER,
    FIELD_BIT_ALLOC,
    FIELD_SCFSI,
    FIELD_SCALEFACTOR,
    FIELD_SAMPLE,
    FIELD_FILLER,
    FIELD_RAW,           /* preamble/trailer — no model */
} FieldType;

/* ---- API ---- */
extern int tcam2_quiet;  /* set to 1 to suppress stderr output */
int  tcam2_compress  (FILE *um2_in, FILE *tcam2_out, int level);
int  tcam2_compress_dict(FILE *in, FILE *out, int level,
                          const unsigned char *dict, size_t dict_size);
int  tcam2_decompress_dict(FILE *in, FILE *out,
                            const unsigned char *dict, size_t dict_size);
int  tcam2_decompress(FILE *tcam2_in, FILE *um2_out);

#endif /* TCAM2_H */
