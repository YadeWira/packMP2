/*  um2_delta_dec.c — Reverse inter-frame delta applied by um2_delta_enc.
    Reads um2 v1.3 (delta-encoded samples), restores absolute sample values,
    emits um2 v1.2 (backward compatible with pack_opt).

    Copyright (C) 2026 Tovy / packMP2 contributors. GPLv3.
*/

#include "um2_delta.h"
#include "unpackmp2.h"

extern int um2_delta_quiet;

static int get_keyframe_interval(uint8_t flags) {
    switch (flags & UM2_KEYFRAME_MASK) {
        case UM2_KEYFRAME_8:  return 8;
        case UM2_KEYFRAME_16: return 16;
        case UM2_KEYFRAME_32: return 32;
        default:               return 64;
    }
}

static int is_keyframe(int frm, int keyframe_interval) {
    return (frm == 0) || (frm % keyframe_interval == 0);
}

int um2_delta_dec_file(FILE *in, FILE *out) {
    int total_frames = 0;
    int i, frm, j, q;

    /* ---- Skip um2 file header and preamble (only present at start of file) ---- */
    {
        int b0 = getc(in);
        int b1 = getc(in);
        int b2 = getc(in);
        int b3 = getc(in);
        if (b0 == 'u' && b1 == 'm' && b2 == '2') {
            /* um2 v1/v2 header found — pass through header + preamble */
            putc(b0, out); putc(b1, out); putc(b2, out); putc(b3, out);
            /* skipped data length (4 bytes BE) */
            int sk0 = getc(in); int sk1 = getc(in);
            int sk2 = getc(in); int sk3 = getc(in);
            putc(sk0, out); putc(sk1, out); putc(sk2, out); putc(sk3, out);
            unsigned int skipped = ((unsigned)sk0 << 24) | (sk1 << 16) | (sk2 << 8) | sk3;
            int c;
            for (unsigned int s = 0; s < skipped; s++) {
                c = getc(in);
                if (c == EOF) return 1;
                putc(c, out);
            }
            if (!um2_delta_quiet) fprintf(stderr, "um2_delta_dec: passed through um2 header + preamble (%u bytes).\n", skipped);
        } else {
            /* No um2 header — rewind */
            if (b0 != EOF) ungetc(b0, in);
            if (b1 != EOF) ungetc(b1, in);
            if (b2 != EOF) ungetc(b2, in);
            if (b3 != EOF) ungetc(b3, in);
        }
    }

    for (;;) {
        /* Read 2-byte frames_in_block (MSB may be set for v1.3).
           If EOF, treat as end of input (no more blocks). */
        int b0 = getc(in);
        int b1 = getc(in);
        if (b0 == EOF || b1 == EOF) {
            if (!um2_delta_quiet)
                fprintf(stderr, "um2_delta_dec: %d frames processed (EOF).\n", total_frames);
            return 0;
        }

        int framesInBlock = ((b0 & 0x7F) << 8) | b1;
        if (framesInBlock == 0) {
            /* Empty block marker — pass through trailer (if any) */
            putc(0, out); putc(0, out);
            /* Trailer: skipped_length (4 bytes BE) + skipped_bytes.
               If EOF reading trailer length, treat as no trailer (success). */
            int t0 = getc(in); int t1 = getc(in);
            int t2 = getc(in); int t3 = getc(in);
            if (t0 == EOF || t1 == EOF || t2 == EOF || t3 == EOF) {
                /* No trailer — return success */
                if (!um2_delta_quiet)
                    fprintf(stderr, "um2_delta_dec: %d frames processed (no trailer).\n", total_frames);
                return 0;
            }
            putc(t0, out); putc(t1, out); putc(t2, out); putc(t3, out);
            unsigned int trailer = ((unsigned)t0 << 24) | (t1 << 16) | (t2 << 8) | t3;
            int c;
            for (unsigned int s = 0; s < trailer; s++) {
                c = getc(in);
                if (c == EOF) return 1;
                putc(c, out);
            }
            if (!um2_delta_quiet)
                fprintf(stderr, "um2_delta_dec: %d frames processed.\n", total_frames);
            return 0;
        }

        int is_v1_3 = (b0 & 0x80) != 0;

        uint8_t flags = 0;
        int keyframe_interval = 64;  /* default; will be overridden by flags if v1.3 */

        if (is_v1_3) {
            flags = getc(in);
            if (flags == EOF) return 1;
            keyframe_interval = get_keyframe_interval(flags);
        }
        /* If not v1.3 (MSB=0), treat as v1.2 — samples are not delta-encoded,
           but we still need to pass through. For simplicity, require v1.3. */
        if (!is_v1_3) {
            fprintf(stderr, "um2_delta_dec: block without v1.3 flag — not delta-encoded.\n");
            return 1;
        }

        if (framesInBlock > MAX_FRAMES_PER_BLOCK) {
            fprintf(stderr, "um2_delta_dec: framesInBlock %d > limit.\n", framesInBlock);
            return 1;
        }

        /* Allocate frame array */
        unpackmp2_t *frames = calloc(framesInBlock, sizeof(unpackmp2_t));
        uint8_t *header_stored = calloc(framesInBlock, sizeof(uint8_t));
        if (!frames || !header_stored) {
            free(frames); free(header_stored);
            return 1;
        }

        /* ---- Read frame headers and bit allocations (same as um2_delta_enc order) ---- */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t *u = &frames[frm];
                if (i == 0) {
                    int b = getc(in);
                    if (b == EOF) { free(frames); free(header_stored); return 1; }
                    if (b == 0xFF) {
                        int j_local;
                        for (j_local = 0; j_local < 4; j_local++) {
                            u->fb[j_local] = b;
                            b = getc(in);
                        }
                        header_stored[frm] = 1;
                    } else {
                        if (frm == 0) {
                            fprintf(stderr, "um2_delta_dec: missing first frame header!\n");
                            free(frames); free(header_stored); return 1;
                        }
                        memcpy(u->fb, (u-1)->fb, 4);
                        header_stored[frm] = 0;
                    }
                    u->hdrLayer = 4 - ((u->fb[1] & 0x06) >> 1);
                    extractFrameHeaderInfo(u);
                    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);
                    /* Now read bit-alloc for sb=0 */
                    if (i < u->sbLimit) {
                        int ba_byte;
                        if (header_stored[frm]) {
                            ba_byte = b;  /* b is already the byte after the 4-byte header */
                        } else {
                            ba_byte = b;  /* b is the bit-alloc for sb=0 */
                        }
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
                    if (b == EOF) { free(frames); free(header_stored); return 1; }
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

        /* ---- Read SCFSI (packed) ---- */
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

        /* ---- Read samples (delta-encoded) and reverse delta ---- */
        int isLayer1 = (framesInBlock > 0 && frames[0].hdrLayer == 1);
        uint16_t prev_abs[2][36][MAX_SBLIMIT];
        memset(prev_abs, 0, sizeof(prev_abs));

        if (isLayer1) {
            for (int bits = 1; bits <= 15; bits++) {
                for (i = 0; i < MAX_SBLIMIT; i++) {
                    for (frm = 0; frm < framesInBlock; frm++) {
                        unpackmp2_t *u = &frames[frm];
                        if (i < u->sbLimit) {
                            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                                if (u->bitalloc2[j][i] != NULL && u->bitalloc2BITS[j][i] + 1 == bits) {
                                    for (q = 0; q < 12; q++) {
                                        uint16_t val = getc(in);
                                        if (bits > 8) val |= getc(in) << 8;
                                        if (is_keyframe(frm, keyframe_interval)) {
                                            u->sampleBITS[j][q][i] = val;
                                        } else {
                                            /* Reverse delta: absolute = prev + delta */
                                            u->sampleBITS[j][q][i] = prev_abs[j][q][i] + (int16_t)val;
                                        }
                                        prev_abs[j][q][i] = u->sampleBITS[j][q][i];
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
                                        uint16_t v0 = getc(in);
                                        if (bits > 8) v0 |= getc(in) << 8;
                                        if (ba->steps == 0) {
                                            uint16_t v1 = getc(in);
                                            uint16_t v2 = getc(in);
                                            if (bits > 8) { v1 |= getc(in) << 8; v2 |= getc(in) << 8; }

                                            if (is_keyframe(frm, keyframe_interval)) {
                                                u->sampleBITS[j][q][i] = v0;
                                                u->sampleBITS[j][q+1][i] = v1;
                                                u->sampleBITS[j][q+2][i] = v2;
                                            } else {
                                                u->sampleBITS[j][q][i] = prev_abs[j][q][i] + (int16_t)v0;
                                                u->sampleBITS[j][q+1][i] = prev_abs[j][q+1][i] + (int16_t)v1;
                                                u->sampleBITS[j][q+2][i] = prev_abs[j][q+2][i] + (int16_t)v2;
                                            }
                                            prev_abs[j][q][i] = u->sampleBITS[j][q][i];
                                            prev_abs[j][q+1][i] = u->sampleBITS[j][q+1][i];
                                            prev_abs[j][q+2][i] = u->sampleBITS[j][q+2][i];
                                        } else {
                                            if (is_keyframe(frm, keyframe_interval)) {
                                                u->sampleBITS[j][q][i] = v0;
                                            } else {
                                                u->sampleBITS[j][q][i] = prev_abs[j][q][i] + (int16_t)v0;
                                            }
                                            prev_abs[j][q][i] = u->sampleBITS[j][q][i];
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* ---- Read filler into fb[] (needed for packFrame) ---- */
        for (frm = 0; frm < framesInBlock; frm++) {
            unpackmp2_t *u = &frames[frm];
            int filler_start = u->fbpos >> 3;
            int filler_len = u->hdrLength - filler_start;
            if (filler_len > 0) {
                int byte_pos = filler_start;
                u->fb[byte_pos] = getc(in);
                for (int b = byte_pos + 1; b < u->hdrLength; b++) {
                    u->fb[b] = getc(in);
                }
            }
        }

        /* ---- Write v1.2 block to output ---- */
        /* Write frame count (native v1.2, no MSB flag) */
        putc(framesInBlock >> 8, out);
        putc(framesInBlock & 0xFF, out);

        /* Write headers and bit-alloc (same interleaved order as pack_opt) */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                const unpackmp2_t *u = &frames[frm];
                /* Guard against 0xFF ambiguity — write header when needed */
                int needs_header = (i == 0) && ((frm == 0) ||
                    (memcmp((u-1)->fb, u->fb, 4) != 0));
                /* Also check if bit-alloc byte would be 0xFF */
                int ba_val = 0;
                if (i < u->sbLimit) {
                    if (i < u->jsBound)
                        ba_val = (u->bitalloc2BITS[1][i] << 4) | u->bitalloc2BITS[0][i];
                    else
                        ba_val = u->bitalloc2BITS[0][i];
                }
                needs_header = needs_header || (ba_val == 0xFF);

                if (needs_header) {
                    int j_local;
                    for (j_local = 0; j_local < 4; j_local++) {
                        putc(u->fb[j_local], out);
                    }
                }
                if (i < u->sbLimit) {
                    putc(ba_val, out);
                }
            }
        }

        /* SCFSI (packed) */
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

        /* Scalefactors (delta-encoded) */
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

        /* Samples (absolute, not delta-encoded — as v1.2 expects) */
        if (isLayer1) {
            for (int bits = 1; bits <= 15; bits++) {
                for (i = 0; i < MAX_SBLIMIT; i++) {
                    for (frm = 0; frm < framesInBlock; frm++) {
                        const unpackmp2_t *u = &frames[frm];
                        if (i < u->sbLimit) {
                            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                                if (u->bitalloc2[j][i] != NULL && u->bitalloc2BITS[j][i] + 1 == bits) {
                                    for (q = 0; q < 12; q++) {
                                        uint16_t val = u->sampleBITS[j][q][i];
                                        putc(val & 0xFF, out);
                                        if (bits > 8) putc((val >> 8) & 0xFF, out);
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
                        const unpackmp2_t *u = &frames[frm];
                        if (i < u->sbLimit) {
                            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                                const sballoc_t *ba = u->bitalloc2[j][i];
                                if (ba != NULL && ba->bits == bits) {
                                    for (q = 0; q < 36; q += 3) {
                                        uint16_t v0 = u->sampleBITS[j][q][i];
                                        putc(v0 & 0xFF, out);
                                        if (bits > 8) putc((v0 >> 8) & 0xFF, out);
                                        if (ba->steps == 0) {
                                            uint16_t v1 = u->sampleBITS[j][q+1][i];
                                            uint16_t v2 = u->sampleBITS[j][q+2][i];
                                            putc(v1 & 0xFF, out);
                                            if (bits > 8) putc((v1 >> 8) & 0xFF, out);
                                            putc(v2 & 0xFF, out);
                                            if (bits > 8) putc((v2 >> 8) & 0xFF, out);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Filler */
        for (frm = 0; frm < framesInBlock; frm++) {
            const unpackmp2_t *u = &frames[frm];
            int filler_start = u->fbpos >> 3;
            int filler_len = u->hdrLength - filler_start;
            if (filler_len > 0) {
                int byte_pos = filler_start;
                putc(u->fb[byte_pos] & ((1 << (8 - (u->fbpos & 7))) - 1), out);
                for (int b = byte_pos + 1; b < u->hdrLength; b++) {
                    putc(u->fb[b], out);
                }
            }
        }

        free(header_stored);
        free(frames);
        total_frames += framesInBlock;
    }

    return 0;
}
