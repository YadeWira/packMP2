/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

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

/* Read unpacked um2 format from infile, repack to mp2, write to outfile. */
int pack(FILE* infile, FILE* outfile) {
    /* check um2 file header */
    if ((getc(infile)!='u') || (getc(infile)!='m') || (getc(infile)!='2') || (getc(infile)!='\1')) {
        fprintf(stderr, "NOT AN UNPACKED MP2 FILE. (missing um2 file header)\n");
        return 5;
    }

    int framecount = 0;
    for (;;) {
        int frm, i, j, q, bits;
        int framesInBlock;

        /* ----- read a block of frames from input file ----- */
        framesInBlock = (getc(infile)<<8) | getc(infile);
        if ((framesInBlock == 0) || (feof(infile))) {
            return 0;   /* EOF (expected) */
        }
        if (framesInBlock > MAX_FRAMES_PER_BLOCK) {
            fprintf(stderr, "error: frames_in_block(%d) is greater than MAX_FRAMES_PER_BLOCK(%d)\n",
                    framesInBlock, MAX_FRAMES_PER_BLOCK);
            return 5;
        }

        /* read frame headers and bit allocations */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
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
                            return 5;
                        }
                        memcpy(u->fb, (u-1)->fb, 4);
                    }
                    b = ungetc(b, infile);
                    if (b == EOF) break;
                    extractFrameHeaderInfo(u);
                    framecount++;
                }
                if (i < u->sbLimit) {
                    const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
                    int b = getc(infile);
                    if (b == EOF) break;
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
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading frame headers and bit allocations! "
                            "(corrupted um2 file or bug?)\n");
            return 5;
        }

        /* read scalefactor selection info and scalefactors */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            int b = getc(infile);
                            if (b == EOF) break;
                            u->scfsiBITS[j][i] = (b>>6) & 3;
                            u->scaleBITS[j][0][i] = b & 0x3F;
                            switch (u->scfsiBITS[j][i]) {
                            case 0:
                                b = getc(infile);
                                u->scaleBITS[j][1][i] = b & 0x3F;
                                if (b>>7 != 0) {
                                    u->scaleBITS[j][2][i] = u->scaleBITS[j][0][i];
                                } else {
                                    u->scaleBITS[j][2][i] = getc(infile);
                                }
                                break;
                            case 1:
                                u->scaleBITS[j][2][i] = getc(infile);
                                break;
                            case 3:
                                u->scaleBITS[j][1][i] = getc(infile);
                                break;
                            }
                        }
                    }
                }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading scalefactors! (corrupted um2 file or bug?)\n");
            return 5;
        }

        /* read samples */
        for (bits = 3; bits <= 16; bits++) {
            for (i = 0; i < MAX_SBLIMIT; i++) {
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &UM2_ARRAY[frm];
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
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading samples! (corrupted um2 file or bug?)\n");
            return 5;
        }
        if (ferror(infile)) {
            perror("read unpacked frames");
            return 5;
        }

        /* ----- write packed data to output file ----- */
        for (frm = 0; frm < framesInBlock; frm++) {
            unpackmp2_t* u = &UM2_ARRAY[frm];
            packFrame(u);
            if ((fwrite(u->fb, 1, u->hdrLength, outfile) != (unsigned)(u->hdrLength))
                || ferror(outfile)) {
                perror("write mp2 frame");
                return 5;
            }
        }
        fprintf(stderr, "packed um2 frames: %d\n", framecount);
    }
    /* NOTREACHED */
    return 0;
}
