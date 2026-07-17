/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009, 2010 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

/* ---- copyData: copy length-prefixed byte block from infile to outfile ---- */
static int copyData(FILE* infile, FILE* outfile) {
    U32 tocopy = 0, i;
    /* read 4-byte big-endian length */
    for (i = 0; i < 4; ++i) { tocopy <<= 8; tocopy |= getc(infile); }
    if (feof(infile)) { return 1; }     /* error: unexpected EOF */
    /* copy bytes verbatim */
    for (i = 0; i < tocopy; ++i) {
        putc(getc(infile), outfile);
        if (feof(infile) || ferror(outfile)) { return 2; }      /* error */
    }
    return 0;   /* OK */
}

/* Re-encode one unpacked frame back into packed MP2 bitstream format. */
void packFrame(unpackmp2_t* u) {
    int i, j, q;

    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);    /* skip header and crc */
    memset(u->fb+(u->fbpos>>3), 0, sizeof(u->fb)-(u->fbpos>>3));

    /* pack bit allocations */
    const char* allocBits = ALLOCTAB_BITS[u->allocTabNum >> 1];
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
            fbputbits(u, u->bitalloc2BITS[j][i], allocBits[i]);
        }
    }

    /* pack scalefactor selection info */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < u->numChannels; j++) {
            if (u->bitalloc2[j][i] != NULL) {
                fbputbits(u, u->scfsiBITS[j][i], 2);
            }
        }
    }
    writeHeaderCRC(u);

    /* pack scalefactors */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < u->numChannels; j++) {
            if (u->bitalloc2[j][i] != NULL) {
                fbputbits(u, u->scaleBITS[j][0][i], 6);
                switch (u->scfsiBITS[j][i]) {
                case 0:
                    fbputbits(u, u->scaleBITS[j][1][i], 6);
                    fbputbits(u, u->scaleBITS[j][2][i], 6);
                    break;
                case 1:
                    fbputbits(u, u->scaleBITS[j][2][i], 6);
                    break;
                case 3:
                    fbputbits(u, u->scaleBITS[j][1][i], 6);
                    break;
                }
            }
        }
    }

    /* pack samples */
    for (q = 0; q < 36; q += 3) {
        for (i = 0; i < u->sbLimit; i++) {
            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                if (bitalloc2 != NULL) {
                    fbputbits(u, u->sampleBITS[j][q][i], bitalloc2->bits);
                    if (bitalloc2->steps == 0) {
                        fbputbits(u, u->sampleBITS[j][q+1][i], bitalloc2->bits);
                        fbputbits(u, u->sampleBITS[j][q+2][i], bitalloc2->bits);
                    }
                }
            }
        }
    }
}

/* Re-encode one unpacked Layer I frame back into packed MP1 bitstream format.
   Reference: ISO 11172-3 §2.4.2, amp11lib amp1dec.cpp (reverse). */
void packFrame_L1(unpackmp2_t* u) {
    int i, j, q;

    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);

    /* Layer I: fixed 4-bit bit allocation per subband */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
            fbputbits(u, u->bitalloc2BITS[j][i], 4);
        }
    }

    /* Layer I: no SCFSI — scalefactor always present if allocated */
    for (i = 0; i < u->sbLimit; i++) {
        for (j = 0; j < u->numChannels; j++) {
            if (u->bitalloc2[j][i] != NULL) {
                fbputbits(u, u->scaleBITS[j][0][i], 6);
            }
        }
    }

    /* Layer I: 12 samples per subband, individual coding */
    for (q = 0; q < 12; q++) {
        for (i = 0; i < u->sbLimit; i++) {
            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                if (u->bitalloc2[j][i] != NULL) {
                    fbputbits(u, u->sampleBITS[j][q][i],
                              u->bitalloc2BITS[j][i] + 1);
                }
            }
        }
    }
}

/* Read um2 v2 from infile, repack to mp2, write to outfile.
   Preserves non-audio data (preamble, filler, trailer) for byte-exact roundtrip. */
int pack_opt(FILE* infile, FILE* outfile, int opt) {
    unpackmp2_t *um2_array = calloc(MAX_FRAMES_PER_BLOCK, sizeof(unpackmp2_t));
    if (!um2_array) return 1;

    /* check um2 v2 file header */
    if ((getc(infile)!='u') || (getc(infile)!='m') || (getc(infile)!='2') || (getc(infile)!=UM2_VERSION)) {
        fprintf(stderr, "NOT AN UNPACKED MP2 FILE. (missing um2 file header)\n");
        free(um2_array);
        return 5;
    }

    /* copy non-audio data before first frame (preamble) */
    if (copyData(infile, outfile) != 0) {
        fprintf(stderr, "error: copy non-audio data before first frame\n");
        free(um2_array);
        return 5;
    }

    int framecount = 0;

    for (;;) {
        int framesInBlock;
        int frm, i, j, q, bits;

        /* ----- read a block of frames from input file ----- */
        framesInBlock = (getc(infile)<<8) | getc(infile);
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading frames_in_block! "
                            "(corrupted um2 file or bug?)\n");
            free(um2_array);
            return 5;
        }
        if (framesInBlock == 0) {   /* no more input data */
            /* copy non-audio data after last frame (trailer) */
            if (copyData(infile, outfile) != 0) {
                fprintf(stderr, "error: copy non-audio data after last frame\n");
                free(um2_array);
                return 5;
            }
            free(um2_array);
            return 0;
        }
        if (framesInBlock > MAX_FRAMES_PER_BLOCK) {
            fprintf(stderr, "error: frames_in_block(%d) is greater than MAX_FRAMES_PER_BLOCK(%d)\n",
                    framesInBlock, MAX_FRAMES_PER_BLOCK);
            free(um2_array);
            return 5;
        }

        /* read frame headers and bit allocations */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &um2_array[frm];
                if (i == 0) {
                    int b = getc(infile);
                    if (b == EOF) break;
                    if (b == 0xFF) {    /* frame header is stored -> read it */
                        int j_local;
                        for (j_local = 0; j_local < 4; j_local++) {
                            u->fb[j_local] = b;
                            b = getc(infile);
                        }
                    } else {    /* frame header not stored -> copy from previous frame */
                        if (frm == 0) {
                            fprintf(stderr, "error: missing first frame header in block! "
                                            "(corrupted um2 file or bug?)\n");
                            free(um2_array);
                            return 5;
                        }
                        memcpy(u->fb, (u-1)->fb, 4);
                    }
                    b = ungetc(b, infile);
                    /* Derive layer from fb[1] (needed for packFrame vs packFrame_L1) */
                    u->hdrLayer = 4 - ((u->fb[1] & 0x06)>>1);
                    if (b == EOF) break;
                    extractFrameHeaderInfo(u);
                    framecount++;
                }
                if (i < u->sbLimit) {
                    int b = getc(infile);
                    if (b == EOF) break;
                    if (u->hdrLayer == 1) {
                        /* Layer I: raw 4-bit bitalloc values (0-14).
                           ALLOC[0]={3,5} used as dummy non-NULL pointer for bitalloc>0. */
                        u->bitalloc2BITS[0][i] = b & 0x0F;
                        u->bitalloc2[0][i] = (b & 0x0F) ? (sballoc_t*)&ALLOC[b & 0x0F] : NULL;
                        if (i < u->jsBound) {
                            u->bitalloc2BITS[1][i] = b >> 4;
                            u->bitalloc2[1][i] = (b >> 4) ? (sballoc_t*)&ALLOC[b >> 4] : NULL;
                        } else { u->bitalloc2[1][i] = u->bitalloc2[0][i]; }
                    } else {
                        const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
                        u->bitalloc2BITS[0][i] = b & 0x0F;
                        u->bitalloc2[0][i] = alloc[i][b & 0x0F];
                        if (i < u->jsBound) {
                            u->bitalloc2BITS[1][i] = b >> 4;
                            u->bitalloc2[1][i] = alloc[i][b >> 4];
                        } else { u->bitalloc2[1][i] = u->bitalloc2[0][i]; }
                    }
                }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading frame headers and bit allocations! "
                            "(corrupted um2 file or bug?)\n");
            free(um2_array);
            return 5;
        }

        /* read scalefactor selection info */
        if (opt) {
            /* Optimized: unpack 4 scfsi values from 1 byte */
            int pack = 0, shift = 8; /* force first read */
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &um2_array[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                if (shift >= 8) { pack = getc(infile); shift = 0; }
                                u->scfsiBITS[j][i] = (pack >> shift) & 3;
                                shift += 2;
                            }
                        }
                    }
                }
            }
        } else {
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &um2_array[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                u->scfsiBITS[j][i] = getc(infile);
                            }
                        }
                    }
                }
            }
        }

        /* read scalefactors (opt: reverse delta from previous frame) */
        {
            unsigned char prev_scale[2][MAX_SBLIMIT];
            memset(prev_scale, 0, sizeof(prev_scale));
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &um2_array[frm];
                    if (i < u->sbLimit) {
                        for (j = 0; j < u->numChannels; j++) {
                            if (u->bitalloc2[j][i] != NULL) {
                                int pred = prev_scale[j][i];
                                int raw = getc(infile);
                                int v0 = opt ? (pred + (signed char)raw) : raw;
                                u->scaleBITS[j][0][i] = v0;
                                prev_scale[j][i] = v0;
                                switch (u->scfsiBITS[j][i]) {
                                case 0: {
                                    int r1 = getc(infile), r2 = getc(infile);
                                    int v1 = opt ? (pred + (signed char)r1) : r1;
                                    int v2 = opt ? (pred + (signed char)r2) : r2;
                                    u->scaleBITS[j][1][i] = v1;
                                    u->scaleBITS[j][2][i] = v2;
                                    break; }
                                case 1: {
                                    int r2 = getc(infile);
                                    u->scaleBITS[j][2][i] = opt ? (pred + (signed char)r2) : r2;
                                    break; }
                                case 3: {
                                    int r1 = getc(infile);
                                    u->scaleBITS[j][1][i] = opt ? (pred + (signed char)r1) : r1;
                                    break; }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading scalefactors! (corrupted um2 file or bug?)\n");
            free(um2_array);
            return 5;
        }

        /* read samples */
        if (framesInBlock > 0 && um2_array[0].hdrLayer == 1) {
            /* Layer I: 12 samples/subband, no grouping, bits=1..15 */
            for (bits = 1; bits <= 15; bits++) {
                for (i = 0; i < MAX_SBLIMIT; i++) {
                    for (frm = 0; frm < framesInBlock; frm++) {
                        unpackmp2_t* u = &um2_array[frm];
                        if (i < u->sbLimit) {
                            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                                if (u->bitalloc2[j][i] != NULL &&
                                    u->bitalloc2BITS[j][i] + 1 == bits) {
                                    for (q = 0; q < 12; q++) {
                                        u->sampleBITS[j][q][i] = getc(infile);
                                        if (bits > 8) {
                                            u->sampleBITS[j][q][i] |= getc(infile)<<8;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            /* Layer II: 36 samples/subband, granule groups, bits=3..16 */
            for (bits = 3; bits <= 16; bits++) {
                for (i = 0; i < MAX_SBLIMIT; i++) {
                    for (frm = 0; frm < framesInBlock; frm++) {
                        unpackmp2_t* u = &um2_array[frm];
                        if (i < u->sbLimit) {
                            for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                                const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                                if ((bitalloc2 != NULL) && (bitalloc2->bits == bits)) {
                                    for (q = 0; q < 36; q += 3) {
                                        u->sampleBITS[j][q][i] = getc(infile);
                                        if (bitalloc2->bits > 8) {
                                            u->sampleBITS[j][q][i] |= getc(infile)<<8;
                                        }
                                        if (bitalloc2->steps == 0) {
                                            u->sampleBITS[j][q+1][i] = getc(infile);
                                            if (bitalloc2->bits > 8) {
                                                u->sampleBITS[j][q+1][i] |= getc(infile)<<8;
                                            }
                                            u->sampleBITS[j][q+2][i] = getc(infile);
                                            if (bitalloc2->bits > 8) {
                                                u->sampleBITS[j][q+2][i] |= getc(infile)<<8;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading samples! (corrupted um2 file or bug?)\n");
            free(um2_array);
            return 5;
        }

        /* repack mp2/mp1 frames; read non-audio "filler" data back into the packed frames */
        for (frm = 0; frm < framesInBlock; frm++) {
            unpackmp2_t* u = &um2_array[frm];
            if (u->hdrLayer == 1)
                packFrame_L1(u);
            else
                packFrame(u);
            if (u->hdrLength > u->fbpos>>3) {
                u->fb[u->fbpos>>3] |= getc(infile);
                for (j = (u->fbpos>>3)+1; j < u->hdrLength; j++) {
                    u->fb[j] = getc(infile);
                }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading filler! (corrupted um2 file or bug?)\n");
            free(um2_array);
            return 5;
        }
        if (ferror(infile)) {
            perror("read unpacked frames");
            free(um2_array);
            return 5;
        }

        /* ----- write packed data to output file ----- */
        for (frm = 0; frm < framesInBlock; frm++) {
            const unpackmp2_t* u = &um2_array[frm];
            if ((fwrite(u->fb, 1, u->hdrLength, outfile) != u->hdrLength) || ferror(outfile)) {
                perror("write mp2 frame");
                free(um2_array);
                return 5;
            }
        }
        if(!unpackmp2_quiet) fprintf(stderr, "packed um2 frames: %d\n", framecount);
    }
    /* NOTREACHED */
    free(um2_array);
    return 0;
}

int pack(FILE* infile, FILE* outfile) {
    return pack_opt(infile, outfile, 0);
}

int pack_optimized(FILE* infile, FILE* outfile) {
    return pack_opt(infile, outfile, 1);
}
