/*  um2_delta_enc.c — Apply inter-frame delta compression to um2 v1.2.
    Strategy: read entire block into memory, apply delta to sampleBITS,
    write block preserving the EXACT same byte structure as unpack.c.
    Uses the unpackmp2_t array to get sample values and re-emit in
    the same order that pack_opt() expects.

    Copyright (C) 2026 Tovy / packMP2 contributors. GPLv3.
*/

#include "um2_delta.h"
#include "unpackmp2.h"
#include <limits.h>

int um2_delta_quiet = 0;

static int is_keyframe(int frm, int keyframe_interval) {
    return (frm == 0) || (frm % keyframe_interval == 0);
}

/* Write a complete delta-encoded block using the EXACT same field ordering
   as unpack.c (lines 242-413). We re-emit everything from the unpackmp2_t
   array after applying delta to sampleBITS.
   hdrStored[frm] = 1 if frame stored a header in the original um2 stream,
                    0 if header was copied from previous frame. */
static int emit_block_v2(FILE *out, unpackmp2_t *frames, int *hdrStored,
                        int framesInBlock, int keyframe_interval) {
    int i, frm, j, q;
    int isLayer1 = (framesInBlock > 0 && frames[0].hdrLayer == 1);

    /* ---- Write v1.3 frame count ---- */
    /* MSB=1 indicates v1.3 format. Upper 7 bits of frame count in b0[6-0]. */
    putc(0x80 | ((framesInBlock >> 8) & 0x7F), out);
    putc(framesInBlock & 0xFF, out);
    /* Build and write flags byte: delta enabled + keyframe interval */
    {
        int ki_flag = 0;
        if (keyframe_interval == 8) ki_flag = 0;
        else if (keyframe_interval == 16) ki_flag = UM2_KEYFRAME_16;
        else if (keyframe_interval == 32) ki_flag = UM2_KEYFRAME_32;
        else ki_flag = UM2_KEYFRAME_64;  /* default 64 */
        int flags = UM2_DELTA_ENABLED | ki_flag;
        putc(flags, out);
    }

    /* ---- Write frame headers and bit allocations (same order as unpack.c) ---- */
    for (i = 0; i < MAX_SBLIMIT; i++) {
        for (frm = 0; frm < framesInBlock; frm++) {
            const unpackmp2_t *u = &frames[frm];

            /* Guard against 0xFF ambiguity — write header only when stored.
               hdrStored tells us exactly whether the original um2 stream
               had a header byte sequence at this position. */
            int needs_header = 0;
            if (i == 0) {
                needs_header = hdrStored[frm];
            }

            if (needs_header) {
                int j_local;
                for (j_local = 0; j_local < 4; j_local++) {
                    putc(u->fb[j_local], out);
                }
            }

            /* Write bit-alloc for this subband */
            if (i < u->sbLimit) {
                if (i < u->jsBound) {
                    int ba = (u->bitalloc2BITS[1][i] << 4) | u->bitalloc2BITS[0][i];
                    putc(ba, out);
                } else {
                    putc(u->bitalloc2BITS[0][i], out);
                }
            }
        }
    }

    /* ---- Write SCFSI (packed 4 per byte when opt=1) ---- */
    {
        int pack = 0, shift = 0;
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                const unpackmp2_t *u = &frames[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            pack |= (u->scfsiBITS[j][i] & 3) << shift;
                            shift += 2;
                            if (shift == 8) { putc(pack, out); pack = 0; shift = 0; }
                        }
                    }
                }
            }
        }
        if (shift > 0) putc(pack, out);
    }

    /* ---- Write scalefactors (delta-encoded, same as unpack.c lines 303-338) ---- */
    {
        unsigned char prev_scale[2][MAX_SBLIMIT];
        memset(prev_scale, 0, sizeof(prev_scale));
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                const unpackmp2_t *u = &frames[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            int pred = prev_scale[j][i];
                            int v0 = u->scaleBITS[j][0][i];
                            putc((unsigned char)((v0 - pred) & 0xFF), out);
                            prev_scale[j][i] = v0;
                            switch (u->scfsiBITS[j][i]) {
                            case 0: {
                                int v1 = u->scaleBITS[j][1][i];
                                int v2 = u->scaleBITS[j][2][i];
                                putc((unsigned char)((v1 - pred) & 0xFF), out);
                                putc((unsigned char)((v2 - pred) & 0xFF), out);
                                break; }
                            case 1: {
                                int v2 = u->scaleBITS[j][2][i];
                                putc((unsigned char)((v2 - pred) & 0xFF), out);
                                break; }
                            case 3: {
                                int v1 = u->scaleBITS[j][1][i];
                                putc((unsigned char)((v1 - pred) & 0xFF), out);
                                break; }
                            }
                        }
                    }
                }
            }
        }
    }

    /* ---- Write samples (delta-encoded when not keyframe) ---- */
    /* prev_abs tracks absolute values across frames within this block */
    uint16_t prev_abs[2][36][MAX_SBLIMIT];
    memset(prev_abs, 0, sizeof(prev_abs));

    if (isLayer1) {
        /* Layer I: 12 samples/subband, grouped by bit-width */
        for (int bits = 1; bits <= 15; bits++) {
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    const unpackmp2_t *u = &frames[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                            if (u->bitalloc2[j][i] != NULL && u->bitalloc2BITS[j][i] + 1 == bits) {
                                int kf = is_keyframe(frm, keyframe_interval);
                                for (q = 0; q < 12; q++) {
                                    uint16_t curr = u->sampleBITS[j][q][i];
                                    uint16_t val;
                                    if (kf) {
                                        val = curr;
                                    } else {
                                        /* Signed delta vs previous frame (same position) */
                                        val = (uint16_t)(int16_t)((int16_t)curr - (int16_t)prev_abs[j][q][i]);
                                    }
                                    putc(val & 0xFF, out);
                                    if (bits > 8) putc((val >> 8) & 0xFF, out);
                                    prev_abs[j][q][i] = curr;
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        /* Layer II: 36 samples/subband, grouped by bit-width */
        for (int bits = 3; bits <= 16; bits++) {
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    const unpackmp2_t *u = &frames[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                            const sballoc_t *ba = u->bitalloc2[j][i];
                            if (ba != NULL && ba->bits == bits) {
                                int kf = is_keyframe(frm, keyframe_interval);
                                for (q = 0; q < 36; q += 3) {
                                    uint16_t v0 = u->sampleBITS[j][q][i];
                                    uint16_t out0;
                                    if (kf) {
                                        out0 = v0;
                                    } else {
                                        out0 = (uint16_t)(int16_t)((int16_t)v0 - (int16_t)prev_abs[j][q][i]);
                                    }
                                    putc(out0 & 0xFF, out);
                                    if (bits > 8) putc((out0 >> 8) & 0xFF, out);
                                    prev_abs[j][q][i] = v0;
                                    if (ba->steps == 0) {
                                        uint16_t v1 = u->sampleBITS[j][q+1][i];
                                        uint16_t v2 = u->sampleBITS[j][q+2][i];
                                        uint16_t out1, out2;
                                        if (kf) {
                                            out1 = v1;
                                            out2 = v2;
                                        } else {
                                            out1 = (uint16_t)(int16_t)((int16_t)v1 - (int16_t)prev_abs[j][q+1][i]);
                                            out2 = (uint16_t)(int16_t)((int16_t)v2 - (int16_t)prev_abs[j][q+2][i]);
                                        }
                                        putc(out1 & 0xFF, out);
                                        if (bits > 8) putc((out1 >> 8) & 0xFF, out);
                                        putc(out2 & 0xFF, out);
                                        if (bits > 8) putc((out2 >> 8) & 0xFF, out);
                                        prev_abs[j][q+1][i] = v1;
                                        prev_abs[j][q+2][i] = v2;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* ---- Write filler (from fb[] buffer) ---- */
    for (frm = 0; frm < framesInBlock; frm++) {
        const unpackmp2_t *u = &frames[frm];
        int filler_start = u->fbpos >> 3;
        int filler_len = u->hdrLength - filler_start;
        if (filler_len > 0) {
            int byte_pos = filler_start;
            /* First filler byte may need partial masking */
            int first_mask = (1 << (8 - (u->fbpos & 7))) - 1;
            if (first_mask != 0xFF) {
                putc(u->fb[byte_pos] & first_mask, out);
                byte_pos++;
                filler_len--;
            }
            for (int b = byte_pos; b < u->hdrLength && filler_len > 0; b++) {
                putc(u->fb[b], out);
                filler_len--;
            }
        }
    }

    return 0;
}

int um2_delta_enc_file(FILE *in, FILE *out, int keyframe_interval) {
    if (keyframe_interval <= 0) keyframe_interval = UM2_DELTA_DEFAULT_KEYFRAME;

    int total_frames = 0;
    int i, frm, j, q;

    /* ---- Skip um2 file header and preamble ---- */
    {
        /* Scan byte by byte looking for 'u','m','2' sequence.
           We use a simple state machine to avoid position-tracking issues. */
        int state = 0;  /* 0=looking for 'u', 1=got 'u' looking for 'm', etc. */
        int b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        int c;
        while ((c = getc(in)) != EOF) {
            if (state == 0 && c == 'u') { state = 1; b0 = c; }
            else if (state == 1 && c == 'm') { state = 2; b1 = c; }
            else if (state == 2 && c == '2') { state = 3; b2 = c; }
            else if (state == 3) { b3 = c; state = 4; break; }  /* found 'um2', b3=version */
            else { state = (c == 'u') ? 1 : 0; b0 = (c == 'u') ? c : 0; b1 = 0; }
        }
        if (state != 4) {
            fprintf(stderr, "um2_delta_enc: missing 'um2' header.\n"); return 1;
        }

        /* At this point b0='u', b1='m', b2='2', b3=version */
        putc(b0, out); putc(b1, out); putc(b2, out); putc(b3, out);
        int sk0 = getc(in); int sk1 = getc(in);
        int sk2 = getc(in); int sk3 = getc(in);
        putc(sk0, out); putc(sk1, out); putc(sk2, out); putc(sk3, out);
        unsigned int skipped = ((unsigned)sk0 << 24) | (sk1 << 16) | (sk2 << 8) | sk3;
        for (unsigned int s = 0; s < skipped; s++) {
            c = getc(in); if (c == EOF) return 1; putc(c, out);
        }
        if (!um2_delta_quiet)
            fprintf(stderr, "um2_delta_enc: passed um2 header + preamble (%u bytes).\n", skipped);
    }

    for (;;) {
        /* Read 2-byte frames_in_block.
           If EOF, treat as end of input — write v1.3 end marker and return. */
        int b0 = getc(in);
        int b1 = getc(in);
        if (b0 == EOF || b1 == EOF) {
            /* Write v1.3 end marker: framesInBlock=0 with MSB set */
            putc(0x80, out); putc(0, out);  /* v1.3 end marker */
            putc(0, out); putc(0, out); putc(0, out); putc(0, out);  /* zero trailer */
            if (!um2_delta_quiet)
                fprintf(stderr, "um2_delta_enc: %d frames processed (EOF).\n", total_frames);
            return 0;
        }

        int framesInBlock = (b0 << 8) | b1;
        if (framesInBlock == 0) {
            /* End marker + trailer (v1.3 format) */
            putc(0x80, out); putc(0, out);  /* v1.3 end marker */
            int t0 = getc(in); int t1 = getc(in);
            int t2 = getc(in); int t3 = getc(in);
            if (t0 == EOF || t1 == EOF || t2 == EOF || t3 == EOF) return 1;
            putc(t0, out); putc(t1, out); putc(t2, out); putc(t3, out);
            unsigned int trailer = ((unsigned)t0 << 24) | (t1 << 16) | (t2 << 8) | t3;
            int c;
            for (unsigned int s = 0; s < trailer; s++) {
                c = getc(in); if (c == EOF) return 1; putc(c, out);
            }
            if (!um2_delta_quiet)
                fprintf(stderr, "um2_delta_enc: %d frames processed.\n", total_frames);
            return 0;
        }

        if (framesInBlock > MAX_FRAMES_PER_BLOCK) {
            fprintf(stderr, "um2_delta_enc: framesInBlock %d > limit.\n", framesInBlock);
            return 1;
        }

        /* Allocate frame array */
        unpackmp2_t *frames = calloc(framesInBlock, sizeof(unpackmp2_t));
        if (!frames) {
            return 1;
        }

        /* ---- Read frame headers and bit allocations (same as pack_opt) ---- */
        /* Track whether each frame stored a header (for bit-alloc reading at i==0) */
        int *hdrStored = calloc(framesInBlock, sizeof(int));
        if (!hdrStored) { free(frames); return 1; }

        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t *u = &frames[frm];
                if (i == 0) {
                    int b = getc(in);
                    if (b == EOF) { free(hdrStored); free(frames); return 1; }

                    /* Determine if decoder will store header: only if byte is 0xFF.
                       If not 0xFF, decoder copies previous frame's header. */
                    if (b == 0xFF) {
                        u->fb[0] = 0xFF;
                        for (int j_local = 1; j_local < 4; j_local++) {
                            u->fb[j_local] = getc(in);
                        }
                        hdrStored[frm] = 1;
                    } else {
                        if (frm == 0) {
                            fprintf(stderr, "um2_delta_enc: missing first frame header!\n");
                            free(hdrStored); free(frames); return 1;
                        }
                        memcpy(u->fb, (u-1)->fb, 4);
                        hdrStored[frm] = 0;
                    }
                    u->hdrLayer = 4 - ((u->fb[1] & 0x06) >> 1);
                    extractFrameHeaderInfo(u);
                    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);

                    /* Read bit-alloc for sb=0.
                       If header was stored: stream is at bit-alloc byte (read fresh).
                       If header was NOT stored: 'b' IS the bit-alloc byte. */
                    if (i < u->sbLimit) {
                        int ba_byte;
                        if (hdrStored[frm]) {
                            ba_byte = getc(in);
                        } else {
                            ba_byte = b;  /* 'b' is the bit-alloc for sb=0 */
                        }
                        if (ba_byte == EOF) { free(hdrStored); free(frames); return 1; }
                        if (u->hdrLayer == 1) {
                            u->bitalloc2BITS[0][i] = ba_byte & 0x0F;
                            u->bitalloc2[0][i] = (ba_byte & 0x0F) ? (sballoc_t*)&ALLOC[ba_byte & 0x0F] : NULL;
                            if (i < u->jsBound) {
                                u->bitalloc2BITS[1][i] = ba_byte >> 4;
                                u->bitalloc2[1][i] = (ba_byte >> 4) ? (sballoc_t*)&ALLOC[ba_byte >> 4] : NULL;
                            } else { u->bitalloc2[1][i] = u->bitalloc2[0][i]; }
                        } else {
                            const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
                            u->bitalloc2BITS[0][i] = ba_byte & 0x0F;
                            u->bitalloc2[0][i] = alloc[i][ba_byte & 0x0F];
                            if (i < u->jsBound) {
                                u->bitalloc2BITS[1][i] = ba_byte >> 4;
                                u->bitalloc2[1][i] = alloc[i][ba_byte >> 4];
                            } else { u->bitalloc2[1][i] = u->bitalloc2[0][i]; }
                        }
                    }
                } else if (i < u->sbLimit) {
                    int b = getc(in);
                    if (b == EOF) { free(hdrStored); free(frames); return 1; }
                    if (u->hdrLayer == 1) {
                        u->bitalloc2BITS[0][i] = b & 0x0F;
                        u->bitalloc2[0][i] = (b & 0x0F) ? (sballoc_t*)&ALLOC[b & 0x0F] : NULL;
                        if (i < u->jsBound) {
                            u->bitalloc2BITS[1][i] = b >> 4;
                            u->bitalloc2[1][i] = (b >> 4) ? (sballoc_t*)&ALLOC[b >> 4] : NULL;
                        } else {
                            u->bitalloc2[1][i] = u->bitalloc2[0][i];
                        }
                    } else {
                        const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
                        u->bitalloc2BITS[0][i] = b & 0x0F;
                        u->bitalloc2[0][i] = alloc[i][b & 0x0F];
                        if (i < u->jsBound) {
                            u->bitalloc2BITS[1][i] = b >> 4;
                            u->bitalloc2[1][i] = alloc[i][b >> 4];
                        } else {
                            u->bitalloc2[1][i] = u->bitalloc2[0][i];
                        }
                    }
                }
            }
        }

        /* ---- Read SCFSI (opt=1, packed) ---- */
        {
            int pack = 0, shift = 8;
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t *u = &frames[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                if (shift >= 8) { pack = getc(in); shift = 0; }
                                u->scfsiBITS[j][i] = (pack >> shift) & 3;
                                shift += 2;
                            }
                        }
                    }
                }
            }
        }

        /* ---- Read scalefactors (delta-decoded to absolute) ---- */
        {
            unsigned char prev_scale[2][MAX_SBLIMIT];
            memset(prev_scale, 0, sizeof(prev_scale));
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t *u = &frames[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                int pred = prev_scale[j][i];
                                int raw = getc(in);
                                int v0 = pred + (signed char)raw;
                                u->scaleBITS[j][0][i] = v0;
                                prev_scale[j][i] = v0;
                                switch (u->scfsiBITS[j][i]) {
                                case 0: {
                                    int r1 = getc(in), r2 = getc(in);
                                    u->scaleBITS[j][1][i] = pred + (signed char)r1;
                                    u->scaleBITS[j][2][i] = pred + (signed char)r2;
                                    break; }
                                case 1: {
                                    int r2 = getc(in);
                                    u->scaleBITS[j][2][i] = pred + (signed char)r2;
                                    break; }
                                case 3: {
                                    int r1 = getc(in);
                                    u->scaleBITS[j][1][i] = pred + (signed char)r1;
                                    break; }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* ---- Read samples into frames array ---- */
        if (framesInBlock > 0 && frames[0].hdrLayer == 1) {
            for (int bits = 1; bits <= 15; bits++) {
                for (i = 0; i < MAX_SBLIMIT; i++) {
                    for (frm = 0; frm < framesInBlock; frm++) {
                        unpackmp2_t *u = &frames[frm];
                        if (i < u->sbLimit) {
                            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                                if (u->bitalloc2[j][i] != NULL && u->bitalloc2BITS[j][i] + 1 == bits) {
                                    for (q = 0; q < 12; q++) {
                                        u->sampleBITS[j][q][i] = getc(in);
                                        if (bits > 8) {
                                            u->sampleBITS[j][q][i] |= getc(in) << 8;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            for (int bits = 3; bits <= 16; bits++) {
                for (i = 0; i < MAX_SBLIMIT; i++) {
                    for (frm = 0; frm < framesInBlock; frm++) {
                        unpackmp2_t *u = &frames[frm];
                        if (i < u->sbLimit) {
                            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                                const sballoc_t *ba = u->bitalloc2[j][i];
                                if (ba != NULL && ba->bits == bits) {
                                    for (q = 0; q < 36; q += 3) {
                                        u->sampleBITS[j][q][i] = getc(in);
                                        if (bits > 8) {
                                            u->sampleBITS[j][q][i] |= getc(in) << 8;
                                        }
                                        if (ba->steps == 0) {
                                            u->sampleBITS[j][q+1][i] = getc(in);
                                            if (bits > 8) u->sampleBITS[j][q+1][i] |= getc(in) << 8;
                                            u->sampleBITS[j][q+2][i] = getc(in);
                                            if (bits > 8) u->sampleBITS[j][q+2][i] |= getc(in) << 8;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* ---- Read filler into fb[] ---- */
        for (frm = 0; frm < framesInBlock; frm++) {
            unpackmp2_t *u = &frames[frm];
            int filler_start = u->fbpos >> 3;
            int filler_len = u->hdrLength - filler_start;
            if (filler_len > 0) {
                u->fb[filler_start] = getc(in);
                for (int b = filler_start + 1; b < u->hdrLength; b++) {
                    u->fb[b] = getc(in);
                }
            }
        }

        /* ---- Pre-apply delta encoding moved into emit_block_v2 (inline).
           prev_abs is per-block, tracked during write to maintain order
           consistency with the decoder's read order. ---- */

        /* ---- Emit delta-encoded block ---- */
        emit_block_v2(out, frames, hdrStored, framesInBlock, keyframe_interval);

        free(hdrStored);
        free(frames);
        total_frames += framesInBlock;
    }
}
