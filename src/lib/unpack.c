/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009, 2010 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

/* Buffer for bytes skipped during frame sync (max 5 MB).
   Stores non-audio data found before/after/between MP2 frames.
   Per-call heap allocation (v0.6) — no longer a static buffer. */
#define MAX_SYNC_SKIP_BYTES  (5*1024*1024)

/* Decompose one packed MP2 frame from the frame buffer into metadata arrays. */
void unpackFrame(unpackmp2_t* u) {
    int i, j, q;

    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);    /* skip header and crc */

    /* unpack bit allocations */
    const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
    const char* allocBits = ALLOCTAB_BITS[u->allocTabNum >> 1];
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
            int bits = fbgetbits(u, allocBits[i]);
            u->bitalloc2BITS[j][i] = bits;
            u->bitalloc2[j][i] = alloc[i][bits];
            if (i >= u->jsBound) {
                u->bitalloc2[1][i] = u->bitalloc2[0][i];
            }
        }
    }

    /* unpack scalefactor selection info */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < u->numChannels; j++) {
            if (u->bitalloc2[j][i] != NULL) {
                u->scfsiBITS[j][i] = fbgetbits(u, 2);
            }
        }
    }

    /* unpack scalefactors */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < u->numChannels; j++) {
            if (u->bitalloc2[j][i] != NULL) {
                u->scaleBITS[j][0][i] = fbgetbits(u, 6);
                switch (u->scfsiBITS[j][i]) {
                case 0:
                    u->scaleBITS[j][1][i] = fbgetbits(u, 6);
                    u->scaleBITS[j][2][i] = fbgetbits(u, 6);
                    break;
                case 1:
                    u->scaleBITS[j][2][i] = fbgetbits(u, 6);
                    break;
                case 3:
                    u->scaleBITS[j][1][i] = fbgetbits(u, 6);
                    break;
                }
            }
        }
    }

    /* unpack samples */
    for (q = 0; q < 36; q += 3) {
        for (i = 0; i < u->sbLimit; i++) {
            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                if (bitalloc2 != NULL) {
                    u->sampleBITS[j][q][i] = fbgetbits(u, bitalloc2->bits);
                    if (bitalloc2->steps == 0) {
                        u->sampleBITS[j][q+1][i] = fbgetbits(u, bitalloc2->bits);
                        u->sampleBITS[j][q+2][i] = fbgetbits(u, bitalloc2->bits);
                    }
                }
            }
        }
    }
}

/* Decompose one packed Layer I MP1 frame from the frame buffer into metadata arrays.
   Layer I differs from Layer II in: fixed 4-bit bit allocation, no SCFSI,
   12 samples per subband (no granule grouping). Reference: amp11lib amp1dec.cpp */
void unpackFrame_L1(unpackmp2_t* u) {
    int i, j, q;

    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);    /* skip header and crc */

    /* Layer I: fixed 4-bit bit allocation per subband (not table-driven) */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
            int bits = fbgetbits(u, 4);
            u->bitalloc2BITS[j][i] = bits;
            /* bits=0 means not allocated. Use ALLOC[bits] as dummy pointer
               (ALLOC[0]={3,5} but we never dereference ->steps for Layer I) */
            u->bitalloc2[j][i] = (bits > 0) ? (sballoc_t*)&ALLOC[bits] : NULL;
            if (i >= u->jsBound) {
                u->bitalloc2[1][i] = u->bitalloc2[0][i];
                u->bitalloc2BITS[1][i] = u->bitalloc2BITS[0][i];
            }
        }
    }

    /* Layer I: no SCFSI — scalefactors always 6 bits for allocated subbands.
       Write same value into all 3 slots for compatibility with Layer II format. */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < u->numChannels; j++) {
            u->scfsiBITS[j][i] = 0;  /* no SCFSI in Layer I */
            if (u->bitalloc2[j][i] != NULL) {
                u->scaleBITS[j][0][i] = fbgetbits(u, 6);
                u->scaleBITS[j][1][i] = u->scaleBITS[j][2][i] = u->scaleBITS[j][0][i];
            }
        }
    }

    /* Layer I: 12 samples per subband, individually coded (no granule groups).
       Sample bit width = bitalloc + 1 (ISO 11172-3 §2.4.2.6). */
    for (q = 0; q < 12; q++) {
        for (i = 0; i < u->sbLimit; i++) {
            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                if (bitalloc2 != NULL) {
                    u->sampleBITS[j][q][i] = fbgetbits(u, u->bitalloc2BITS[j][i] + 1);
                }
            }
        }
    }
}

/* Read mp2 from infile, unpack to um2 v2 format, write to outfile.
   Preserves all non-audio data (preamble, filler, trailer) for
   byte-exact roundtrip. */
int unpack_opt(FILE* infile, FILE* outfile, int opt) {
    U32 framecount = 0;
    U32 skipped = 0;

    unpackmp2_t *um2_array = malloc(MAX_FRAMES_PER_BLOCK * sizeof(unpackmp2_t));
    unsigned char *skipped_data = malloc(MAX_SYNC_SKIP_BYTES);
    if (!um2_array || !skipped_data) {
        free(um2_array); free(skipped_data);
        return 1;
    }

    for (;;) {
        int framesInBlock = 0;

        /* ----- read a block of frames from input file ----- */
        for (framesInBlock = 0; framesInBlock < MAX_FRAMES_PER_BLOCK; framesInBlock++) {
            int b0 = getc(infile);
            int b1 = getc(infile);
            int b2 = getc(infile);
            int b3 = EOF;

            /* check um2 file header (input is already unpacked) */
            if ((framecount==0) && (b0=='u') && (b1=='m') && (b2=='2')) {
                fprintf(stderr, "NOT AN MP2 FILE. (found um2 file header)\n");
                free(um2_array); free(skipped_data);
                return 4;
            }

            /* sync to frame header; capture non-MP2 bytes */
            skipped = 0;
            while (skipped <= MAX_SYNC_SKIP_BYTES) {
                b3 = getc(infile);
                if ((b0 == EOF) || (b1 == EOF) || (b2 == EOF) || (b3 == EOF)) {
                    if (framecount == 0) {
                        fprintf(stderr, "NOT AN MP2 FILE. (sync frame header, skipped %d bytes, EOF.)\n",
                                skipped);
                        free(um2_array); free(skipped_data);
                        return 4;
                    } else {
                        if (b0 != EOF) { skipped_data[skipped++] = b0; }
                        if (b1 != EOF) { skipped_data[skipped++] = b1; }
                        if (b2 != EOF) { skipped_data[skipped++] = b2; }
                        break;
                    }
                } else {
                    if ( (b0 == 0xFF) &&
                         (((b1&0xFE) == 0xFC) || ((b1&0xFE) == 0xF4) ||    /* MPEG-1 or MPEG-2 Layer II */
                          ((b1&0xFE) == 0xFE) || ((b1&0xFE) == 0xF6)) &&   /* MPEG-1 or MPEG-2 Layer I */
                         ((b2&0xF0) != 0x00) &&    /* bitrate != free */
                         ((b2&0xF0) != 0xF0) &&    /* bitrate != bad */
                         ((b2&0x0C) != 0x0C)       /* frequency != reserved */
                    ) {
                        break;      /* OK - frame sync! */
                    } else {
                        skipped_data[skipped++] = b0;   /* no frame sync */
                        b0 = b1;
                        b1 = b2;
                        b2 = b3;
                    }
                }
            }
            if (skipped >= MAX_SYNC_SKIP_BYTES) {
                if (framecount == 0) {
                    fprintf(stderr, "NOT AN MP2 FILE. (sync frame header, skipped %d bytes, no sync.)\n",
                            skipped);
                    free(um2_array); free(skipped_data);
                    return 4;
                } else {
                    break;    /* lost sync */
                }
            }
            if (skipped != 0) {
                if(!unpackmp2_quiet) fprintf(stderr, "skipped(%d) frame(%d)\n", skipped, framecount);
            }
            if (feof(infile)) {
                break;
            }

            unpackmp2_t* u = &um2_array[framesInBlock];
            u->fb[0] = b0;
            u->fb[1] = b1;
            u->fb[2] = b2;
            u->fb[3] = b3;
            extractFrameHeaderInfo(u);

            if ((fread(&u->fb[4], 1, u->hdrLength-4, infile) != (unsigned)(u->hdrLength-4))
                || feof(infile)) {
                fprintf(stderr, "read mp2 frame: EOF.\n");
                break;
            } else if (ferror(infile)) {
                perror("read mp2 frame");
                break;
            }
            if (u->hdrLayer == 1)
                unpackFrame_L1(u);
            else
                unpackFrame(u);

            /* first frame: write um2 header + preamble (non-audio data before first frame) */
            if (framecount == 0) {
                putc('u', outfile); putc('m', outfile);
                putc('2', outfile); putc(UM2_VERSION, outfile);
                /* write non-audio data before first frame */
                putc(skipped>>24, outfile); putc(skipped>>16, outfile);
                putc(skipped>>8,  outfile); putc(skipped,     outfile);
                { U32 i; for (i = 0; i < skipped; ++i) { putc(skipped_data[i], outfile); } }
            }
            ++framecount;
        }

        /* ----- write unpacked data to output file ----- */
        putc(framesInBlock>>8, outfile);
        putc(framesInBlock,    outfile);

        int frm, i, j, q, bits;

        /* write bit allocations */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                const unpackmp2_t* u = &um2_array[frm];
                if (i < u->sbLimit) {
                    if (i < u->jsBound) {
                        q = (u->bitalloc2BITS[1][i]<<4) | u->bitalloc2BITS[0][i];
                    } else {
                        q = u->bitalloc2BITS[0][i];
                    }
                } else {
                    q = 0;
                }
                /* write frame header when needed; also guard against 0xFF ambiguity */
                if ((i == 0) && ((frm == 0) || (memcmp((u-1)->fb, u->fb, 4) != 0) || (q == 0xFF))) {
                    for (j = 0; j < 4; j++) { putc(u->fb[j], outfile); }
                }
                if (i < u->sbLimit) { putc(q, outfile); }
            }
        }

        /* write scalefactor selection info */
        if (opt) {
            /* Optimized: pack 4 scfsi values per byte (each is 0-3 = 2 bits) */
            int pack = 0, shift = 0;
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &um2_array[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                pack |= (u->scfsiBITS[j][i] & 3) << shift;
                                shift += 2;
                                if (shift == 8) { putc(pack, outfile); pack = 0; shift = 0; }
                            }
                        }
                    }
                }
            }
            if (shift > 0) putc(pack, outfile); /* flush remaining */
        } else {
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &um2_array[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                putc(u->scfsiBITS[j][i], outfile);
                            }
                        }
                    }
                }
            }
        }

        /* write scalefactors (opt: delta-encoded from previous frame) */
        {
            unsigned char prev_scale[2][MAX_SBLIMIT]; /* ch, subband */
            memset(prev_scale, 0, sizeof(prev_scale));
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    const unpackmp2_t* u = &um2_array[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                int pred = prev_scale[j][i];
                                int v0 = u->scaleBITS[j][0][i];
                                putc(opt ? (unsigned char)((v0 - pred) & 0xFF) : v0, outfile);
                                prev_scale[j][i] = v0;
                                switch (u->scfsiBITS[j][i]) {
                                case 0: {
                                    int v1 = u->scaleBITS[j][1][i];
                                    int v2 = u->scaleBITS[j][2][i];
                                    putc(opt ? (unsigned char)((v1 - pred) & 0xFF) : v1, outfile);
                                    putc(opt ? (unsigned char)((v2 - pred) & 0xFF) : v2, outfile);
                                    break; }
                                case 1: {
                                    int v2 = u->scaleBITS[j][2][i];
                                    putc(opt ? (unsigned char)((v2 - pred) & 0xFF) : v2, outfile);
                                    break; }
                                case 3: {
                                    int v1 = u->scaleBITS[j][1][i];
                                    putc(opt ? (unsigned char)((v1 - pred) & 0xFF) : v1, outfile);
                                    break; }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* write samples */
        for (bits = 3; bits <= 16; bits++) {
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &um2_array[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                            const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                            if ((bitalloc2 != NULL) && (bitalloc2->bits == bits)) {
                                for (q = 0; q < 36; q += 3) {
                                    putc(u->sampleBITS[j][q][i], outfile);
                                    if (bitalloc2->bits > 8) {
                                        putc(u->sampleBITS[j][q][i] >> 8, outfile);
                                    }
                                    if (bitalloc2->steps == 0) {
                                        putc(u->sampleBITS[j][q+1][i], outfile);
                                        if (bitalloc2->bits > 8) {
                                            putc(u->sampleBITS[j][q+1][i] >> 8, outfile);
                                        }
                                        putc(u->sampleBITS[j][q+2][i], outfile);
                                        if (bitalloc2->bits > 8) {
                                            putc(u->sampleBITS[j][q+2][i] >> 8, outfile);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        /* write non-audio "filler" data from inside the mp2 frames */
        for (frm = 0; frm < framesInBlock; frm++) {
            const unpackmp2_t* u = &um2_array[frm];
            i = u->hdrLength - (u->fbpos>>3);
            if (i < 0) {
                fprintf(stderr, "error: too many bits read from frame "
                        "(length=%d, bitpos=%d)\ncorrupted mp2 file or bug?\n",
                        u->hdrLength, u->fbpos);
                free(um2_array); free(skipped_data);
                return 4;
            } else if (i > 0) {
                putc(u->fb[u->fbpos>>3] & ((1<<(8-(u->fbpos&7)))-1), outfile);
                for (j = (u->fbpos>>3)+1; j < u->hdrLength; j++) {
                    putc(u->fb[j], outfile);
                }
            }
        }
        if(!unpackmp2_quiet) fprintf(stderr, "unpacked mp2 frames: %d\n", framecount);
        if (ferror(outfile)) {
            perror("write unpacked frame");
            free(um2_array); free(skipped_data);
            return 4;
        }

        /* ----- end of input file ----- */
        if (feof(infile)) {
            /* write "empty block" */
            putc(0, outfile); putc(0, outfile);
            /* write non-audio data after last frame */
            putc(skipped>>24, outfile); putc(skipped>>16, outfile);
            putc(skipped>>8,  outfile); putc(skipped,     outfile);
            { U32 i; for (i = 0; i < skipped; ++i) { putc(skipped_data[i], outfile); } }
            free(um2_array); free(skipped_data);
            return 0;
        }
    }
    /* NOTREACHED */
    free(um2_array); free(skipped_data);
    return 0;
}

int unpack(FILE* infile, FILE* outfile) {
    return unpack_opt(infile, outfile, 0);
}

int unpack_optimized(FILE* infile, FILE* outfile) {
    return unpack_opt(infile, outfile, 1);
}
