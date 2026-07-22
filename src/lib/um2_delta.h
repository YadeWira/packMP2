/*  um2_delta — Inter-frame delta compression layer for um2 v1.3
    Applies temporal differencing between consecutive frames in a block.
    Usage:
      cat file.um2 | um2_delta_enc > file_d.um2
      cat file_d.um2 | um2_delta_dec > file.um2

    Copyright (C) 2026 Tovy / packMP2 contributors. GPLv3.
*/

#ifndef UM2_DELTA_H
#define UM2_DELTA_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- um2 v1.3 flags ---- */
#define UM2_VERSION_1_3         '\3'   /* version byte in magic header */
#define UM2_DELTA_ENABLED       0x01   /* bit 0: inter-frame delta active */
#define UM2_KEYFRAME_MASK       0x06   /* bits 1-2: keyframe interval */
#define UM2_KEYFRAME_8         0x00   /* keyframe every  8 frames */
#define UM2_KEYFRAME_16        0x02   /* keyframe every 16 frames */
#define UM2_KEYFRAME_32        0x04   /* keyframe every 32 frames */
#define UM2_KEYFRAME_64        0x06   /* keyframe every 64 frames (default) */
#define UM2_DELTA_SIGNED       0x00   /* signed 16-bit diff (default) */
#define UM2_DELTA_XOR          0x10   /* bitwise XOR (alternative) */

/* Default keyframe interval: 64 frames */
#define UM2_DELTA_DEFAULT_KEYFRAME  64

/* ---- API ---- */
extern int um2_delta_quiet;  /* set to 1 to suppress stderr output */

/* Compress um2 v1.2 → um2 v1.3 (apply inter-frame delta) */
int um2_delta_enc_file(FILE *in, FILE *out, int keyframe_interval);

/* Decompress um2 v1.3 → um2 v1.2 (reverse inter-frame delta) */
int um2_delta_dec_file(FILE *in, FILE *out);

#endif /* UM2_DELTA_H */
