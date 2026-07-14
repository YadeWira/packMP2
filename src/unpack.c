/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

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

/* Read mp2 from infile, unpack to um2 format, write to outfile. */
int unpack(FILE* infile, FILE* outfile) {
    const int MAX_SYNC_SKIP_BYTES = 512*1024;
    int framecount = 0;

    for (;;) {
        int framesInBlock = 0;
        int frm;

        /* ----- read a block of frames from input file ----- */
        for (framesInBlock = 0; framesInBlock < MAX_FRAMES_PER_BLOCK; framesInBlock++) {
            unpackmp2_t* u = &UM2_ARRAY[framesInBlock];
            int skipped = 0;
            int b0 = getc(infile);
            int b1 = getc(infile);
            int b2 = getc(infile);
            int b3 = EOF;

            /* check um2 file header */
            if ((framecount==0) && (b0=='u') && (b1=='m') && (b2=='2')) {
                fprintf(stderr, "NOT AN MP2 FILE. (found um2 file header)\n");
                return 4;
            }

            /* sync to frame header */
            while (skipped <= MAX_SYNC_SKIP_BYTES) {
                b3 = getc(infile);
                if ((b0 == EOF) || (b1 == EOF) || (b2 == EOF) || (b3 == EOF)) {
                    if (framecount == 0) {
                        fprintf(stderr, "NOT AN MP2 FILE. (sync frame header, skipped %d bytes, EOF.)\n",
                                skipped);
                        return 4;
                    } else {
                        break;    /* EOF */
                    }
                } else {
                    if ( (b0 == 0xFF) &&
                         (((b1&0xFE) == 0xFC) || ((b1&0xFE) == 0xF4)) &&    /* MPEG-1 or MPEG-2 Layer2 */
                         ((b2&0xF0) != 0x00) &&    /* bitrate != free */
                         ((b2&0xF0) != 0xF0) &&    /* bitrate != bad */
                         ((b2&0x0C) != 0x0C)       /* frequency != reserved */
                    ) {
                        break;      /* OK - frame sync! */
                    } else {
                        skipped++;
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
                    return 4;
                } else {
                    break;    /* lost sync */
                }
            }
            if (skipped != 0) {
                fprintf(stderr, "skipped(%d) frame(%d)\n", skipped, framecount);
            }
            if (feof(infile)) {
                break;
            }

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
            unpackFrame(u);
            ++framecount;
        }
        if (framesInBlock == 0) {   /* end of input exactly on MAX_FRAMES_PER_BLOCK boundary */
            return 0;
        }

        /* ----- write unpacked data to output file ----- */
        if (framecount <= MAX_FRAMES_PER_BLOCK) {
            putc('u', outfile); putc('m', outfile);
            putc('2', outfile); putc('\1', outfile);    /* write um2 file header */
        }
        putc(framesInBlock>>8, outfile);
        putc(framesInBlock,    outfile);

        int i, j, q, bits;

        /* write frame headers and bit allocations */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if ((i == 0) && ((frm == 0) || (memcmp((u-1)->fb, u->fb, 4) != 0))) {
                    int j_local;
                    for (j_local = 0; j_local < 4; j_local++) {
                        putc(u->fb[j_local], outfile);
                    }
                }
                if (i < u->sbLimit) {
                    if (i < u->jsBound) {
                        putc((u->bitalloc2BITS[1][i]<<4) | u->bitalloc2BITS[0][i], outfile);
                    } else {
                        putc(u->bitalloc2BITS[0][i], outfile);
                    }
                }
            }
        }

        /* write scalefactor selection info and scalefactors */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            putc((u->scfsiBITS[j][i]<<6) | u->scaleBITS[j][0][i], outfile);
                            switch (u->scfsiBITS[j][i]) {
                            case 0:
                                if (u->scaleBITS[j][0][i] == u->scaleBITS[j][2][i]) {
                                    putc((1<<7) | u->scaleBITS[j][1][i], outfile);
                                } else {
                                    putc(u->scaleBITS[j][1][i], outfile);
                                    putc(u->scaleBITS[j][2][i], outfile);
                                }
                                break;
                            case 1:
                                putc(u->scaleBITS[j][2][i], outfile);
                                break;
                            case 3:
                                putc(u->scaleBITS[j][1][i], outfile);
                                break;
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
                    unpackmp2_t* u = &UM2_ARRAY[frm];
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
        if (ferror(outfile)) {
            perror("write unpacked frame");
            return 4;
        }
        fprintf(stderr, "unpacked mp2 frames: %d\n", framecount);
    }
    /* NOTREACHED */
    return 0;
}
