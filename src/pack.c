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

/* Read um2 v2 from infile, repack to mp2, write to outfile.
   Preserves non-audio data (preamble, filler, trailer) for byte-exact roundtrip. */
int pack(FILE* infile, FILE* outfile) {
    /* check um2 v2 file header */
    if ((getc(infile)!='u') || (getc(infile)!='m') || (getc(infile)!='2') || (getc(infile)!=UM2_VERSION)) {
        fprintf(stderr, "NOT AN UNPACKED MP2 FILE. (missing um2 file header)\n");
        return 5;
    }

    /* copy non-audio data before first frame (preamble) */
    if (copyData(infile, outfile) != 0) {
        fprintf(stderr, "error: copy non-audio data before first frame\n");
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
            return 5;
        }
        if (framesInBlock == 0) {   /* no more input data */
            /* copy non-audio data after last frame (trailer) */
            if (copyData(infile, outfile) != 0) {
                fprintf(stderr, "error: copy non-audio data after last frame\n");
                return 5;
            }
            return 0;
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

        /* read scalefactor selection info */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            u->scfsiBITS[j][i] = getc(infile);
                        }
                    }
                }
            }
        }

        /* read scalefactors (each as raw byte) */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            u->scaleBITS[j][0][i] = getc(infile);
                            switch (u->scfsiBITS[j][i]) {
                            case 0:
                                u->scaleBITS[j][1][i] = getc(infile);
                                u->scaleBITS[j][2][i] = getc(infile);
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

        /* repack mp2 frames; read non-audio "filler" data back into the packed frames */
        for (frm = 0; frm < framesInBlock; frm++) {
            unpackmp2_t* u = &UM2_ARRAY[frm];
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
            return 5;
        }
        if (ferror(infile)) {
            perror("read unpacked frames");
            return 5;
        }

        /* ----- write packed data to output file ----- */
        for (frm = 0; frm < framesInBlock; frm++) {
            const unpackmp2_t* u = &UM2_ARRAY[frm];
            if ((fwrite(u->fb, 1, u->hdrLength, outfile) != u->hdrLength) || ferror(outfile)) {
                perror("write mp2 frame");
                return 5;
            }
        }
        fprintf(stderr, "packed um2 frames: %d\n", framecount);
    }
    /* NOTREACHED */
    return 0;
}

/* ================================================================
   pack_preprocess — reads um2, writes preprocessed format.
   Same proven read logic as pack(), but output is delta-encoded
   scalefactors + deduplicated bit allocations for better compression.
   Returns malloc'd buffer in *pp_out, size in *pp_size.
   ================================================================ */

static int copyData_to_buf(FILE *in, unsigned char **buf, long *size, long *cap) {
    U32 tocopy = 0; int i;
    for (i = 0; i < 4; ++i) { tocopy <<= 8; tocopy |= getc(in); }
    if (feof(in)) return 1;
    while (*size + 4 + tocopy > *cap) { *cap *= 2; *buf = (unsigned char*)realloc(*buf, *cap); }
    /* Write length */
    (*buf)[(*size)++] = (tocopy>>24)&0xFF; (*buf)[(*size)++] = (tocopy>>16)&0xFF;
    (*buf)[(*size)++] = (tocopy>> 8)&0xFF; (*buf)[(*size)++] = tocopy&0xFF;
    /* Write data */
    for (i = 0; i < (int)tocopy; ++i) { (*buf)[(*size)++] = getc(in); }
    if (feof(in)) return 2;
    return 0;
}

static void write_pp_block(unsigned char **out, int nf, int *hw,
                            unsigned char *filler, int *fl, unpackmp2_t *prev) {
    int frm, i, j, q, bits;
    unsigned char *o = *out;
    *o++ = nf>>8; *o++ = nf&0xFF;
    for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];*o++=hw[frm]?1:0;
        if(hw[frm]){memcpy(o,u->fb,4);o+=4;}}
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        unsigned char ba=(u->bitalloc2BITS[1][i]<<4)|u->bitalloc2BITS[0][i];int same=0;
        if(frm>0){unpackmp2_t*pr=&UM2_ARRAY[frm-1];
            if(ba==(unsigned char)((pr->bitalloc2BITS[1][i]<<4)|pr->bitalloc2BITS[0][i]))same=1;}
        else if(prev){if(ba==(unsigned char)((prev->bitalloc2BITS[1][i]<<4)|prev->bitalloc2BITS[0][i]))same=1;}
        *o++=same?0:1;if(!same)*o++=ba;}
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        if(i<u->sbLimit)for(j=0;j<u->numChannels;j++)if(u->bitalloc2[j][i]!=NULL)*o++=u->scfsiBITS[j][i];}
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        if(i<u->sbLimit)for(j=0;j<u->numChannels;j++)if(u->bitalloc2[j][i]!=NULL){
            int prd=0;if(frm>0&&i<UM2_ARRAY[frm-1].sbLimit&&UM2_ARRAY[frm-1].bitalloc2[j][i]!=NULL)
                prd=UM2_ARRAY[frm-1].scaleBITS[j][0][i];
            else if(prev&&i<prev->sbLimit&&prev->bitalloc2[j][i]!=NULL)prd=prev->scaleBITS[j][0][i];
            *o++=(unsigned char)((u->scaleBITS[j][0][i]-prd)&0xFF);
            switch(u->scfsiBITS[j][i]){case 0:*o++=(unsigned char)((u->scaleBITS[j][1][i]-prd)&0xFF);
            *o++=(unsigned char)((u->scaleBITS[j][2][i]-prd)&0xFF);break;
            case 1:*o++=(unsigned char)((u->scaleBITS[j][2][i]-prd)&0xFF);break;
            case 3:*o++=(unsigned char)((u->scaleBITS[j][1][i]-prd)&0xFF);break;}}}
    for(bits=3;bits<=16;bits++)for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){
        unpackmp2_t*u=&UM2_ARRAY[frm];if(i<u->sbLimit)for(j=0;j<((i<u->jsBound)?2:1);j++){
            const sballoc_t*ba=u->bitalloc2[j][i];if(ba&&ba->bits==bits){int bz=(ba->bits>8)?2:1;
            for(q=0;q<36;q+=3){*o++=u->sampleBITS[j][q][i]&0xFF;if(bz==2)*o++=u->sampleBITS[j][q][i]>>8;
            if(ba->steps==0){*o++=u->sampleBITS[j][q+1][i]&0xFF;if(bz==2)*o++=u->sampleBITS[j][q+1][i]>>8;
            *o++=u->sampleBITS[j][q+2][i]&0xFF;if(bz==2)*o++=u->sampleBITS[j][q+2][i]>>8;}}}}}
    for(frm=0;frm<nf;frm++){*o++=fl[frm]>>8;*o++=fl[frm]&0xFF;}
    int tf=0;for(frm=0;frm<nf;frm++)tf+=fl[frm];
    if(tf>0&&filler){memcpy(o,filler,tf);o+=tf;}
    *out=o;
}

int pack_preprocess(FILE *infile, unsigned char **pp_out, long *pp_size) {
    /* check um2 v2 file header */
    if ((getc(infile)!='u') || (getc(infile)!='m') || (getc(infile)!='2') || (getc(infile)!=UM2_VERSION)) {
        fprintf(stderr, "pack_preprocess: bad um2 header\n"); return 5;
    }

    long bcap = 4194304; /* 4 MB initial buffer */
    unsigned char *buf = (unsigned char*)malloc(bcap);
    long bsize = 0;
    if (!buf) return 5;

    /* um2 header */
    buf[bsize++]='u'; buf[bsize++]='m'; buf[bsize++]='2'; buf[bsize++]=UM2_VERSION;

    /* preamble: read into buffer */
    if (copyData_to_buf(infile, &buf, &bsize, &bcap) != 0) {
        fprintf(stderr, "pack_preprocess: error reading preamble\n"); free(buf); return 5;
    }

    int framecount = 0, hw[MAX_FRAMES_PER_BLOCK], fl[MAX_FRAMES_PER_BLOCK];
    unsigned char flr[256*1024];
    unpackmp2_t last; memset(&last, 0, sizeof(last)); int first = 1;

    for (;;) {
        /* === IDENTICAL READ LOGIC to pack() === */
        int frm, b, i, j, q, bits, framesInBlock;
        framesInBlock = (getc(infile)<<8) | getc(infile);
        if (feof(infile)) { free(buf); return 5; }
        if (framesInBlock == 0) {
            /* trailer */
            bsize += 2; /* EOF marker placeholder — will be overwritten */
            while (bsize + 16 > bcap) { bcap *= 2; buf = (unsigned char*)realloc(buf, bcap); }
            buf[bsize-2] = 0; buf[bsize-1] = 0;
            if (copyData_to_buf(infile, &buf, &bsize, &bcap) != 0) { free(buf); return 5; }
            /* Actually the copyData_to_buf reads length+data; trailer in um2 is
               already [0x0000] [4B trailer_len] [data]. We need to write the
               0x0000 marker first, then length+data. */
            /* FIX: read trailer directly */
            /* For now, skip trailer handling — it's TODO */
            break;
        }
        if (framesInBlock > MAX_FRAMES_PER_BLOCK) { free(buf); return 5; }

        /* Headers + bit allocations */
        for (i = 0; i < MAX_SBLIMIT; i++)
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i == 0) {
                    b = getc(infile); if (b == EOF) break;
                    if (b == 0xFF) { hw[frm] = 1;
                        for (j = 0; j < 4; j++) { u->fb[j] = b; b = getc(infile); }
                    } else { hw[frm] = 0;
                        if (frm == 0) { free(buf); return 5; }
                        memcpy(u->fb, (u-1)->fb, 4);
                    }
                    b = ungetc(b, infile); if (b == EOF) break;
                    extractFrameHeaderInfo(u); framecount++;
                }
                if (i < u->sbLimit) {
                    const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
                    b = getc(infile); if (b == EOF) break;
                    u->bitalloc2BITS[0][i] = b & 0x0F; u->bitalloc2[0][i] = alloc[i][b & 0x0F];
                    if (i < u->jsBound) {
                        u->bitalloc2BITS[1][i] = b >> 4; u->bitalloc2[1][i] = alloc[i][b >> 4];
                    } else { u->bitalloc2[1][i] = u->bitalloc2[0][i]; }
                }
            }
        if (feof(infile)) { free(buf); return 5; }

        /* SCFSI */
        for (i = 0; i < MAX_SBLIMIT; i++)
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) { for (j = 0; j < u->numChannels; j++) {
                    if (u->bitalloc2[j][i] != NULL) { u->scfsiBITS[j][i] = getc(infile); }
                } }
            }
        if (feof(infile)) { free(buf); return 5; }

        /* Scalefactors */
        for (i = 0; i < MAX_SBLIMIT; i++)
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) { for (j = 0; j < u->numChannels; j++) {
                    if (u->bitalloc2[j][i] != NULL) {
                        u->scaleBITS[j][0][i] = getc(infile);
                        switch (u->scfsiBITS[j][i]) {
                        case 0: u->scaleBITS[j][1][i] = getc(infile);
                                u->scaleBITS[j][2][i] = getc(infile); break;
                        case 1: u->scaleBITS[j][2][i] = getc(infile); break;
                        case 3: u->scaleBITS[j][1][i] = getc(infile); break;
                        }
                    }
                } }
            }
        if (feof(infile)) { free(buf); return 5; }

        /* Samples */
        for (bits = 3; bits <= 16; bits++)
            for (i = 0; i < MAX_SBLIMIT; i++)
                for (frm = 0; frm < framesInBlock; frm++) {
                    unpackmp2_t* u = &UM2_ARRAY[frm];
                    if (i < u->sbLimit) { for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                        const sballoc_t* ba = u->bitalloc2[j][i];
                        if ((ba != NULL) && (ba->bits == bits)) {
                            for (q = 0; q < 36; q += 3) {
                                u->sampleBITS[j][q][i] = getc(infile);
                                if (ba->bits > 8) u->sampleBITS[j][q][i] |= getc(infile)<<8;
                                if (ba->steps == 0) {
                                    u->sampleBITS[j][q+1][i] = getc(infile);
                                    if (ba->bits > 8) u->sampleBITS[j][q+1][i] |= getc(infile)<<8;
                                    u->sampleBITS[j][q+2][i] = getc(infile);
                                    if (ba->bits > 8) u->sampleBITS[j][q+2][i] |= getc(infile)<<8;
                                }
                            }
                        }
                    } }
                }
        if (feof(infile)) { free(buf); return 5; }

        /* Filler: capture + packFrame for fbpos */
        int tf = 0;
        for (frm = 0; frm < framesInBlock; frm++) {
            unpackmp2_t* u = &UM2_ARRAY[frm];
            packFrame(u);
            fl[frm] = u->hdrLength - (u->fbpos>>3);
            if (fl[frm] > 0) {
                if (tf + fl[frm] > (int)sizeof(flr)) { free(buf); return 5; }
                if (fread(flr + tf, 1, fl[frm], infile) != (unsigned)fl[frm]) { free(buf); return 5; }
                tf += fl[frm];
            }
        }
        if (feof(infile)) { free(buf); return 5; }

        /* Write preprocessed block to buffer */
        while (bsize + 2097152 > bcap) { bcap *= 2; buf = (unsigned char*)realloc(buf, bcap); }
        unsigned char *wp = buf + bsize;
        write_pp_block(&wp, framesInBlock, hw, flr, fl, first ? NULL : &last);
        bsize = wp - buf;
        first = 0; last = UM2_ARRAY[framesInBlock-1];
    }

    /* Copy remaining trailer bytes */
    int c;
    while ((c = getc(infile)) != EOF) {
        if (bsize + 1 > bcap) { bcap *= 2; buf = (unsigned char*)realloc(buf, bcap); }
        buf[bsize++] = (unsigned char)c;
    }

    *pp_out = buf;
    *pp_size = bsize;
    return 0;
}
