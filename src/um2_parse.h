/*  um2_parse.h — shared um2 v1.2 block parser for TCAM2 encoder/decoder.
    Parses memory buffer into global UM2_ARRAY with frame metadata.
    Copyright (C) 2026 Tovy. GPLv3.
*/
#ifndef UM2_PARSE_H
#define UM2_PARSE_H

#include "unpackmp2.h"

/* Parse one um2 v1.2 block from memory.
   *p: pointer to current position in um2 byte stream (updated)
   end: end of buffer
   hdr_written: output, per-frame flag whether header was stored (1) or copied (0)
   filler_buf: output buffer for captured filler bytes (may be NULL to discard)
   filler_lens: output, filler byte count per frame

   Returns number of frames in block, 0 for EOF marker, -1 on error.
   On success, UM2_ARRAY[0..nframes-1] is populated with frame data
   (headers, bit allocs, scfsi, scalefactors, samples, fbpos). */
int um2_parse_block(unsigned char **p, unsigned char *end,
                     int *hdr_written, unsigned char *filler_buf,
                     int *filler_lens);

#endif
