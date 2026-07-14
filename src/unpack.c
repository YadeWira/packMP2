/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009, 2010 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

/* Buffer for bytes skipped during frame sync (max 5 MB).
   Stores non-audio data found before/after/between MP2 frames. */
#define MAX_SYNC_SKIP_BYTES  (5*1024*1024)
static unsigned char SKIPPED_DATA[MAX_SYNC_SKIP_BYTES];

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

/* Read mp2 from infile, unpack to um2 v2 format, write to outfile.
   Preserves all non-audio data (preamble, filler, trailer) for
   byte-exact roundtrip. */
int unpack(FILE* infile, FILE* outfile) {
    U32 framecount = 0;
    U32 skipped = 0;

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
                        return 4;
                    } else {
                        if (b0 != EOF) { SKIPPED_DATA[skipped++] = b0; }
                        if (b1 != EOF) { SKIPPED_DATA[skipped++] = b1; }
                        if (b2 != EOF) { SKIPPED_DATA[skipped++] = b2; }
                        break;
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
                        SKIPPED_DATA[skipped++] = b0;   /* no frame sync */
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

            unpackmp2_t* u = &UM2_ARRAY[framesInBlock];
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

            /* first frame: write um2 header + preamble (non-audio data before first frame) */
            if (framecount == 0) {
                putc('u', outfile); putc('m', outfile);
                putc('2', outfile); putc(UM2_VERSION, outfile);
                /* write non-audio data before first frame */
                putc(skipped>>24, outfile); putc(skipped>>16, outfile);
                putc(skipped>>8,  outfile); putc(skipped,     outfile);
                { U32 i; for (i = 0; i < skipped; ++i) { putc(SKIPPED_DATA[i], outfile); } }
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
                const unpackmp2_t* u = &UM2_ARRAY[frm];
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
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            putc(u->scfsiBITS[j][i], outfile);
                        }
                    }
                }
            }
        }

        /* write scalefactors (each as raw byte, no v1.1 optimization) */
        for (i = 0; i < MAX_SBLIMIT; i++) {
            for (frm = 0; frm < framesInBlock; frm++) {
                const unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) {
                    for (j = 0; j < u->numChannels; j++) {
                        if (u->bitalloc2[j][i] != NULL) {
                            putc(u->scaleBITS[j][0][i], outfile);
                            switch (u->scfsiBITS[j][i]) {
                            case 0:
                                putc(u->scaleBITS[j][1][i], outfile);
                                putc(u->scaleBITS[j][2][i], outfile);
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

        /* write non-audio "filler" data from inside the mp2 frames */
        for (frm = 0; frm < framesInBlock; frm++) {
            const unpackmp2_t* u = &UM2_ARRAY[frm];
            i = u->hdrLength - (u->fbpos>>3);
            if (i < 0) {
                fprintf(stderr, "error: too many bits read from frame "
                        "(length=%d, bitpos=%d)\ncorrupted mp2 file or bug?\n",
                        u->hdrLength, u->fbpos);
                return 4;
            } else if (i > 0) {
                putc(u->fb[u->fbpos>>3] & ((1<<(8-(u->fbpos&7)))-1), outfile);
                for (j = (u->fbpos>>3)+1; j < u->hdrLength; j++) {
                    putc(u->fb[j], outfile);
                }
            }
        }
        fprintf(stderr, "unpacked mp2 frames: %d\n", framecount);
        if (ferror(outfile)) {
            perror("write unpacked frame");
            return 4;
        }

        /* ----- end of input file ----- */
        if (feof(infile)) {
            /* write "empty block" */
            putc(0, outfile); putc(0, outfile);
            /* write non-audio data after last frame */
            putc(skipped>>24, outfile); putc(skipped>>16, outfile);
            putc(skipped>>8,  outfile); putc(skipped,     outfile);
            { U32 i; for (i = 0; i < skipped; ++i) { putc(SKIPPED_DATA[i], outfile); } }
            return 0;
        }
    }
    /* NOTREACHED */
    return 0;
}

/* ================================================================

/* ================================================================
   unpack_from_pp — reads preprocessed format, reverses transforms,
   writes um2 v1.2.  Counterpart to pack_preprocess() in pack.c.
   ================================================================ */

static int read_pp_block(unsigned char **p, unsigned char *end,
                          int *hw, unsigned char *flr, int *fl,
                          unpackmp2_t *prev) {
    unsigned char *s = *p; if (s+2 > end) return -1;
    int nf = (s[0]<<8)|s[1]; s += 2;
    if (nf == 0) return 0;
    if (nf > MAX_FRAMES_PER_BLOCK) return -1;
    int frm, i, j, q, bits, tf = 0;

    for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];if(s>=end)return -1;hw[frm]=*s++;
        if(hw[frm]){if(s+4>end)return -1;u->fb[0]=s[0];u->fb[1]=s[1];u->fb[2]=s[2];u->fb[3]=s[3];s+=4;}
        else{if(frm==0){if(!prev)return -1;memcpy(u->fb,prev->fb,4);}
             else memcpy(u->fb,(u-1)->fb,4);}extractFrameHeaderInfo(u);}
    /* Bit allocs: sbLimit per frame + flags for subbands within limit */
    for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        if(s>=end)return -1;int lim=*s++;
        for(i=0;i<lim;i++){
            if(s>=end)return -1;int ex=*s++;unsigned char ba;
            if(ex){if(s>=end)return -1;ba=*s++;}
            else{if(frm>0){unpackmp2_t*pr=&UM2_ARRAY[frm-1];
                ba=(pr->bitalloc2BITS[1][i]<<4)|pr->bitalloc2BITS[0][i];}
                else if(prev){ba=(prev->bitalloc2BITS[1][i]<<4)|prev->bitalloc2BITS[0][i];}else ba=0;}
            if(i<u->sbLimit){const sballoc_t***alc=ALLOCTABS[u->allocTabNum>>1];
                u->bitalloc2BITS[0][i]=ba&0x0F;u->bitalloc2[0][i]=alc[i][ba&0x0F];
                if(i<u->jsBound){u->bitalloc2BITS[1][i]=ba>>4;u->bitalloc2[1][i]=alc[i][ba>>4];}
                else{u->bitalloc2[1][i]=u->bitalloc2[0][i];u->bitalloc2BITS[1][i]=u->bitalloc2BITS[0][i];}}}}
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        if(i<u->sbLimit)for(j=0;j<u->numChannels;j++)if(u->bitalloc2[j][i]!=NULL)
        {if(s>=end)return -1;u->scfsiBITS[j][i]=*s++;}}
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        if(i<u->sbLimit)for(j=0;j<u->numChannels;j++)if(u->bitalloc2[j][i]!=NULL){
            int prd=0;if(frm>0&&i<UM2_ARRAY[frm-1].sbLimit&&UM2_ARRAY[frm-1].bitalloc2[j][i])
                prd=UM2_ARRAY[frm-1].scaleBITS[j][0][i];
            else if(prev&&i<prev->sbLimit&&prev->bitalloc2[j][i])prd=prev->scaleBITS[j][0][i];
            if(s>=end)return -1;u->scaleBITS[j][0][i]=(unsigned char)(prd+(signed char)*s++);
            switch(u->scfsiBITS[j][i]){case 0:if(s>=end)return -1;
                u->scaleBITS[j][1][i]=(unsigned char)(prd+(signed char)*s++);if(s>=end)return -1;
                u->scaleBITS[j][2][i]=(unsigned char)(prd+(signed char)*s++);break;
            case 1:if(s>=end)return -1;
                u->scaleBITS[j][2][i]=(unsigned char)(prd+(signed char)*s++);break;
            case 3:if(s>=end)return -1;
                u->scaleBITS[j][1][i]=(unsigned char)(prd+(signed char)*s++);break;}}}
    for(bits=3;bits<=16;bits++)for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){
        unpackmp2_t*u=&UM2_ARRAY[frm];if(i<u->sbLimit)for(j=0;j<((i<u->jsBound)?2:1);j++){
            const sballoc_t*ba=u->bitalloc2[j][i];if(ba&&ba->bits==bits){int bz=(ba->bits>8)?2:1;
            for(q=0;q<36;q+=3){if(s+bz>end)return -1;u->sampleBITS[j][q][i]=s[0];
                if(bz==2)u->sampleBITS[j][q][i]|=s[1]<<8;s+=bz;if(ba->steps==0){if(s+bz*2>end)return -1;
                u->sampleBITS[j][q+1][i]=s[0];if(bz==2)u->sampleBITS[j][q+1][i]|=s[1]<<8;s+=bz;
                u->sampleBITS[j][q+2][i]=s[0];if(bz==2)u->sampleBITS[j][q+2][i]|=s[1]<<8;s+=bz;}}}}}
    for(frm=0;frm<nf;frm++){if(s+2>end)return -1;fl[frm]=(s[0]<<8)|s[1];s+=2;}
    for(frm=0;frm<nf;frm++){int flen=fl[frm];if(flen>0){if(s+flen>end)return -1;
        if(flr){memcpy(flr+tf,s,flen);tf+=flen;}s+=flen;}}
    *p = s; return nf;
}

/* Write um2 block from UM2_ARRAY (same logic as unpack.c write phase) */
static void write_um2_block(FILE *out, int nf, int *hw,
                             unsigned char *flr, int *fl) {
    int i, j, q, bits, frm, fp = 0;
    putc(nf>>8, out); putc(nf&0xFF, out);
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){const unpackmp2_t*u=&UM2_ARRAY[frm];
        if(i==0){unsigned char ba=0;if(0<u->sbLimit){if(0<u->jsBound)
            ba=(u->bitalloc2BITS[1][0]<<4)|u->bitalloc2BITS[0][0];else ba=u->bitalloc2BITS[0][0];}
            int nd=(frm==0)||(memcmp((u-1)->fb,u->fb,4)!=0)||(ba==0xFF);
            if(nd){putc(0xFF,out);putc(u->fb[1],out);putc(u->fb[2],out);putc(u->fb[3],out);}}
        if(i<u->sbLimit){if(i<u->jsBound)putc((u->bitalloc2BITS[1][i]<<4)|u->bitalloc2BITS[0][i],out);
            else putc(u->bitalloc2BITS[0][i],out);}}
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        if(i<u->sbLimit)for(j=0;j<u->numChannels;j++)if(u->bitalloc2[j][i]!=NULL)putc(u->scfsiBITS[j][i],out);}
    for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){unpackmp2_t*u=&UM2_ARRAY[frm];
        if(i<u->sbLimit)for(j=0;j<u->numChannels;j++)if(u->bitalloc2[j][i]!=NULL){
            putc(u->scaleBITS[j][0][i],out);switch(u->scfsiBITS[j][i]){case 0:putc(u->scaleBITS[j][1][i],out);
            putc(u->scaleBITS[j][2][i],out);break;case 1:putc(u->scaleBITS[j][2][i],out);break;
            case 3:putc(u->scaleBITS[j][1][i],out);break;}}}
    for(bits=3;bits<=16;bits++)for(i=0;i<MAX_SBLIMIT;i++)for(frm=0;frm<nf;frm++){
        unpackmp2_t*u=&UM2_ARRAY[frm];if(i<u->sbLimit)for(j=0;j<((i<u->jsBound)?2:1);j++){
            const sballoc_t*ba=u->bitalloc2[j][i];if(ba&&ba->bits==bits){int bz=(ba->bits>8)?2:1;
            for(q=0;q<36;q+=3){putc(u->sampleBITS[j][q][i]&0xFF,out);if(bz==2)putc(u->sampleBITS[j][q][i]>>8,out);
            if(ba->steps==0){putc(u->sampleBITS[j][q+1][i]&0xFF,out);if(bz==2)putc(u->sampleBITS[j][q+1][i]>>8,out);
            putc(u->sampleBITS[j][q+2][i]&0xFF,out);if(bz==2)putc(u->sampleBITS[j][q+2][i]>>8,out);}}}}}
    for(frm=0;frm<nf;frm++){int flen=fl[frm];if(flen>0){fwrite(flr+fp,1,flen,out);fp+=flen;}}
}

int unpack_from_pp(unsigned char *pp, long pp_size, FILE *out) {
    unsigned char *p = pp, *end = pp + pp_size;
    if (p+4>end || p[0]!='u'||p[1]!='m'||p[2]!='2'||p[3]!=UM2_VERSION)
    { fprintf(stderr,"unpack_from_pp: bad header\n"); return 1; }
    fwrite(p, 1, 4, out); p += 4;
    if (p+4>end) return 1;
    unsigned long pre_len = ((U32)p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
    fwrite(p, 1, 4, out); p += 4;
    if (p+pre_len > end) return 1;
    fwrite(p, 1, pre_len, out); p += pre_len;

    int hw[MAX_FRAMES_PER_BLOCK], fl[MAX_FRAMES_PER_BLOCK];
    unsigned char flr[256*1024];
    unpackmp2_t last; memset(&last, 0, sizeof(last)); int first = 1;

    while (p < end) {
        int nf = read_pp_block(&p, end, hw, flr, fl, first ? NULL : &last);
        if (nf < 0) return 1;
        if (nf == 0) { fwrite(p, 1, end - p, out); break; }
        write_um2_block(out, nf, hw, flr, fl);
        first = 0; last = UM2_ARRAY[nf-1];
    }
    return 0;
}
