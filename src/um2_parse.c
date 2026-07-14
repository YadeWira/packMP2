/*  um2_parse.c — shared um2 v1.2 block parser.
    Parses one block at a time from memory into global UM2_ARRAY.
    Copyright (C) 2026 Tovy. GPLv3.
*/
#include "um2_parse.h"

int um2_parse_block(unsigned char **p, unsigned char *end,
                     int *hdr_w, unsigned char *filler_buf, int *flens) {
    unsigned char *s = *p;
    if (s + 2 > end) return -1;
    int nf = (s[0] << 8) | s[1]; s += 2;
    if (nf == 0) return 0;
    if (nf > MAX_FRAMES_PER_BLOCK) return -1;

    int frm, i, j, q, bits, tf = 0;

    /* Headers + bit allocations */
    for (i = 0; i < MAX_SBLIMIT; i++)
        for (frm = 0; frm < nf; frm++) {
            unpackmp2_t *u = &UM2_ARRAY[frm];
            if (i == 0) {
                if (s >= end) return -1;
                int b = *s++;
                if (b == 0xFF) {
                    hdr_w[frm] = 1;
                    if (s + 3 > end) return -1;
                    u->fb[0] = 0xFF; u->fb[1] = s[0]; u->fb[2] = s[1]; u->fb[3] = s[2];
                    s += 3;
                } else {
                    hdr_w[frm] = 0;
                    if (frm == 0) return -1;
                    memcpy(u->fb, (u-1)->fb, 4);
                }
                extractFrameHeaderInfo(u);
            }
            if (i < u->sbLimit) {
                if (s >= end) return -1;
                int b = *s++;
                const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
                u->bitalloc2BITS[0][i] = b & 0x0F;
                u->bitalloc2[0][i]     = alloc[i][b & 0x0F];
                if (i < u->jsBound) {
                    u->bitalloc2BITS[1][i] = b >> 4;
                    u->bitalloc2[1][i]     = alloc[i][b >> 4];
                } else {
                    u->bitalloc2[1][i] = u->bitalloc2[0][i];
                    u->bitalloc2BITS[1][i] = u->bitalloc2BITS[0][i];
                }
            }
        }

    /* SCFSI */
    for (i = 0; i < MAX_SBLIMIT; i++)
        for (frm = 0; frm < nf; frm++) {
            unpackmp2_t *u = &UM2_ARRAY[frm];
            if (i < u->sbLimit)
                for (j = 0; j < u->numChannels; j++)
                    if (u->bitalloc2[j][i] != NULL) {
                        if (s >= end) return -1;
                        u->scfsiBITS[j][i] = *s++;
                    }
        }

    /* Scalefactors */
    for (i = 0; i < MAX_SBLIMIT; i++)
        for (frm = 0; frm < nf; frm++) {
            unpackmp2_t *u = &UM2_ARRAY[frm];
            if (i < u->sbLimit)
                for (j = 0; j < u->numChannels; j++)
                    if (u->bitalloc2[j][i] != NULL) {
                        if (s >= end) return -1;
                        u->scaleBITS[j][0][i] = *s++;
                        switch (u->scfsiBITS[j][i]) {
                        case 0: if (s+1>=end) return -1;
                            u->scaleBITS[j][1][i] = *s++;
                            u->scaleBITS[j][2][i] = *s++; break;
                        case 1: if (s>=end) return -1;
                            u->scaleBITS[j][2][i] = *s++; break;
                        case 3: if (s>=end) return -1;
                            u->scaleBITS[j][1][i] = *s++; break;
                        }
                    }
        }

    /* Samples */
    for (bits = 3; bits <= 16; bits++)
        for (i = 0; i < MAX_SBLIMIT; i++)
            for (frm = 0; frm < nf; frm++) {
                unpackmp2_t *u = &UM2_ARRAY[frm];
                if (i < u->sbLimit)
                    for (j = 0; j < ((i < u->jsBound) ? 2 : 1); j++) {
                        const sballoc_t *ba = u->bitalloc2[j][i];
                        if (ba && ba->bits == bits) {
                            int bsz = (ba->bits > 8) ? 2 : 1;
                            for (q = 0; q < 36; q += 3) {
                                if (s + bsz > end) return -1;
                                u->sampleBITS[j][q][i] = s[0];
                                if (bsz == 2) u->sampleBITS[j][q][i] |= s[1] << 8;
                                s += bsz;
                                if (ba->steps == 0) {
                                    if (s + bsz*2 > end) return -1;
                                    u->sampleBITS[j][q+1][i] = s[0];
                                    if (bsz == 2) u->sampleBITS[j][q+1][i] |= s[1] << 8;
                                    s += bsz;
                                    u->sampleBITS[j][q+2][i] = s[0];
                                    if (bsz == 2) u->sampleBITS[j][q+2][i] |= s[1] << 8;
                                    s += bsz;
                                }
                            }
                        }
                    }
            }

    /* Compute fbpos via packFrame() (canonical, proven correct) + capture filler */
    for (frm = 0; frm < nf; frm++) {
        unpackmp2_t *u = &UM2_ARRAY[frm];
        /* Save frame buffer region that packFrame() modifies */
        unsigned char saved[1792];
        memcpy(saved, u->fb, sizeof(u->fb));
        packFrame(u);  /* computes correct fbpos */
        unsigned real_pos = u->fbpos;
        memcpy(u->fb, saved, sizeof(u->fb));  /* restore */
        u->fbpos = real_pos;  /* keep computed fbpos */
        int flen = u->hdrLength - (real_pos>>3);
        if (flens) flens[frm] = flen;
        if (flen > 0) {
            if (s + flen > end) return -1;
            if (filler_buf) { memcpy(filler_buf + tf, s, flen); tf += flen; }
            s += flen;
        }
    }

    *p = s;
    return nf;
}
