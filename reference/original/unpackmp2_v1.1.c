/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009 Michael Henke

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/*  This program uses code/ideas from:
    - amp11 by Niklas Beisert
    - libmad by Underbit Technologies, Inc.
    - libtwolame by TwoLAME Authors
    
    The included Windows executable was compiled with MinGW (gcc 4.4.0):
    gcc unpackmp2.c -o unpackmp2 -Wall -O2 -fomit-frame-pointer -march=i686 -static-libgcc -s
*/
/*  Changes:
    v1.0 (2009-10-04)
    -initial release
    
    v1.0.1 (2009-11-10)
    -added MPEG frame protection (CRC)
    
    v1.1 (2009-12-04)
    -improved compression by re-ordering the unpacked data
     (format of unpacked files is not compatible with previous version!)
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
# include <fcntl.h>
# include <io.h>
# define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
# define SET_BINARY_MODE(file)
#endif

typedef unsigned int U32;   // 32 bit unsigned integer

#define MAX_SBLIMIT 30

const char FRMHDR_LSF[4] = {2, 3, 1, 0};   //MPEG-2.5, reserved, MPEG-2, MPEG-1
const int FRMHDR_FREQUENCY[4] = {44100, 48000, 32000, -1};
const short FRMHDR_BITRATE[2][16] = {
    {-1, 32, 48, 56, 64, 80, 96,112,128,160,192,224,256,320,384,-1},    //MPEG-1 LayerII
    {-1,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160,-1}     //MPEG-2 LSF LayerII
};
typedef struct {
    char steps;
    char bits;
} sballoc_t;
const sballoc_t ALLOC[17] = {
    {3,5}, {5,7}, {9,10},
    {0,3}, {0,4}, {0,5}, {0,6}, {0,7}, {0,8}, {0,9}, {0,10}, {0,11}, {0,12}, {0,13}, {0,14}, {0,15}, {0,16}
};
const sballoc_t* ATAB0[16] = {
    NULL     ,&ALLOC[0], &ALLOC[3], &ALLOC[4], &ALLOC[5], &ALLOC[6], &ALLOC[7], &ALLOC[8],
    &ALLOC[9],&ALLOC[10],&ALLOC[11],&ALLOC[12],&ALLOC[13],&ALLOC[14],&ALLOC[15],&ALLOC[16]
};
const sballoc_t* ATAB1[16] = {
    NULL     ,&ALLOC[0], &ALLOC[1], &ALLOC[3], &ALLOC[2], &ALLOC[4], &ALLOC[5], &ALLOC[6],
    &ALLOC[7],&ALLOC[8], &ALLOC[9], &ALLOC[10],&ALLOC[11],&ALLOC[12],&ALLOC[13],&ALLOC[16]
};
const sballoc_t* ATAB2[8]  = {
    NULL     ,&ALLOC[0], &ALLOC[1], &ALLOC[3], &ALLOC[2], &ALLOC[4], &ALLOC[5], &ALLOC[16]
};
const sballoc_t* ATAB3[4]  = {
    NULL     ,&ALLOC[0], &ALLOC[1], &ALLOC[16]
};
const sballoc_t* ATAB4[16] = {
    NULL     ,&ALLOC[0], &ALLOC[1], &ALLOC[2], &ALLOC[4], &ALLOC[5], &ALLOC[6], &ALLOC[7],
    &ALLOC[8],&ALLOC[9], &ALLOC[10],&ALLOC[11],&ALLOC[12],&ALLOC[13],&ALLOC[14],&ALLOC[15]
};
const sballoc_t* ATAB5[16] = {
    NULL     ,&ALLOC[0], &ALLOC[1], &ALLOC[3], &ALLOC[2], &ALLOC[4], &ALLOC[5], &ALLOC[6],
    &ALLOC[7],&ALLOC[8], &ALLOC[9], &ALLOC[10],&ALLOC[11],&ALLOC[12],&ALLOC[13],&ALLOC[14]
};
const sballoc_t** ALLOCTABS[3][MAX_SBLIMIT] = {
    { ATAB0, ATAB0, ATAB0, ATAB1, ATAB1, ATAB1, ATAB1, ATAB1, ATAB1, ATAB1,
      ATAB1, ATAB2, ATAB2, ATAB2, ATAB2, ATAB2, ATAB2, ATAB2, ATAB2, ATAB2,
      ATAB2, ATAB2, ATAB2, ATAB3, ATAB3, ATAB3, ATAB3, ATAB3, ATAB3, ATAB3  },
    { ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4,
      ATAB4, ATAB4                                                          },
    { ATAB5, ATAB5, ATAB5, ATAB5, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4,
      ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4,
      ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4, ATAB4  }
};
const char ALLOCTAB_LENGTHS[5] = {27, 30, 8, 12, 30};
const char ALLOCTAB_BITS[3][MAX_SBLIMIT] = {
    {4,4,4,4,4,4,4,4,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2},
    {4,4,3,3,3,3,3,3,3,3,3,3},
    {4,4,4,4,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2}
};


typedef struct {
    unsigned char fb[1728+64];  //frame buffer, max. frame length: 384kbps/32kHz + safety margin;
    unsigned int fbpos;         //bit position (used by functions fbgetbits / fbputbits)
    unsigned int hdrFrequency;  //translated using FRMHDR_FREQUENCY: (44100/48000/32000) MPEG-1, (22050/24000/16000) MPEG-2
    const sballoc_t* bitalloc2[2][MAX_SBLIMIT];
    unsigned char bitalloc2BITS[2][MAX_SBLIMIT];     //values have 2...4 bits
    unsigned char scfsiBITS[2][MAX_SBLIMIT];         //values have     2 bits
    unsigned char scaleBITS[2][3][MAX_SBLIMIT];      //values have     6 bits
    unsigned short sampleBITS[2][36][MAX_SBLIMIT];   //values have ...16 bits
    unsigned short hdrLength;   //calculated using hdrBitrate and hdrFrequency, in Bytes
    unsigned short hdrBitrate;  //translated using FRMHDR_BITRATE: (32...384) MPEG-1, (8...160) MPEG-2
    unsigned char hdrLsf;       //translated using FRMHDR_LSF: 0=MPEG-1, 1=MPEG-2
    unsigned char hdrMode;      //directly from header: 0=Stereo, 1=Joint stereo, 2=Dual channel, 3=Single channel (Mono)
    unsigned char hdrModeExt;   //directly from header: use intensity stereo for bands: 0=4...31, 1=8...31, 2=12...31, 3=16...31
    unsigned char hdrHasCrc;    //directly from header (inverted): true=frame has CRC
    unsigned char numChannels;  //number of channels, calculated using hdrMode: 1=Mono, 2=Dual/Joint/Stereo
    unsigned char allocTabNum;  //number of the bit allocation table
    unsigned char sbLimit;      //scalefactor band limit, translated using allocTabNum in ALLOCTAB_LENGTHS
    unsigned char jsBound;      //joint stereo boundary, calculated using hdrMode and hdrModeExt or sbLimit
} unpackmp2_t;


//MAX_FRAMES_PER_BLOCK must be < 65536 (16 bits)
#define MAX_FRAMES_PER_BLOCK  4096
unpackmp2_t UM2_ARRAY[MAX_FRAMES_PER_BLOCK];


//*****************************************************************************
U32 fbgetbits(unpackmp2_t* u, int n) {     //n = 0...16
    U32 result = 0;
    if (n > 0) {
        result = ( (((U32)(u->fb[(u->fbpos>>3)+0])<<24) |
                    ((U32)(u->fb[(u->fbpos>>3)+1])<<16) |
                    ((U32)(u->fb[(u->fbpos>>3)+2])<< 8)) >> (32-(u->fbpos&7)-n) ) & ((1<<n)-1);
        u->fbpos += n;
    }
    return result;
}


//*****************************************************************************
void fbputbits(unpackmp2_t* u, U32 v, int n) {    //n = 0...16
    if (n > 0) {
        v &= (1<<n)-1;
        v <<= 32-(u->fbpos&7)-n;
        u->fb[(u->fbpos>>3)+0] |= v>>24;
        u->fb[(u->fbpos>>3)+1]  = v>>16;
        u->fb[(u->fbpos>>3)+2]  = v>> 8;
        u->fbpos += n;
    }
}


//*****************************************************************************
void extractFrameHeaderInfo(unpackmp2_t* u) {
    u->hdrLsf = FRMHDR_LSF[(u->fb[1] & 0x18)>>3];
    u->hdrFrequency = FRMHDR_FREQUENCY[(u->fb[2] & 0x0C)>>2] >> u->hdrLsf;
    u->hdrBitrate = FRMHDR_BITRATE[(u->hdrLsf==0 ? 0 : 1)][(u->fb[2] & 0xF0)>>4];
    u->hdrLength = 144000 * u->hdrBitrate / u->hdrFrequency + ((u->fb[2] & 0x02)>>1);
    u->hdrMode = (u->fb[3] &0xC0)>>6;
    u->hdrModeExt = (u->fb[3] &0x30)>>4;
    u->hdrHasCrc = ((u->fb[1] & 0x01) == 0);
    u->numChannels = (u->hdrMode==3) ? 1 : 2;
    int brpch = u->hdrBitrate / u->numChannels;
    if (u->hdrLsf != 0)   u->allocTabNum = 4;
    else if (brpch <= 48) u->allocTabNum = (u->hdrFrequency == 32000) ? 3 : 2;
    else if (brpch <= 80) u->allocTabNum = 0;
    else                  u->allocTabNum = (u->hdrFrequency == 48000) ? 0 : 1;
    u->sbLimit = ALLOCTAB_LENGTHS[(int)u->allocTabNum];
    u->jsBound = u->sbLimit;
    if (u->hdrMode == 1)      u->jsBound = (u->hdrModeExt + 1) * 4;
    else if (u->hdrMode == 3) u->jsBound = 0;
}
//*****************************************************************************
void printFrameHeaderInfo(unpackmp2_t* u, int frameNumber) {
    fprintf(stderr, " frame(%d) : lsf(%d) freq(%d) bitrate(%d) framelength(%d) mode(%d) modeExt(%d) crc(%d) sblimit(%d) jsbound(%d)\n",
    frameNumber, u->hdrLsf, u->hdrFrequency, u->hdrBitrate, u->hdrLength, u->hdrMode, u->hdrModeExt, u->hdrHasCrc, u->sbLimit, u->jsBound);
}


//*****************************************************************************
U32 updateCRC(U32 crc, unsigned char value, int n) {
    U32 t = value << 8;
    for ( ; n > 0; --n) {
        t <<= 1;
        crc <<= 1;
        if (((crc ^ t) & 0x10000)) {
            crc ^= 0x8005;
        }
    }
    return crc;
}
//*****************************************************************************
void writeHeaderCRC(unpackmp2_t* u) {
    if ( ! u->hdrHasCrc ) return;
    U32 crc = 0xffff;
    crc = updateCRC(crc, u->fb[2], 8);
    crc = updateCRC(crc, u->fb[3], 8);
    int i;
    for (i=4+2; i < u->fbpos>>3; i++) {
        crc = updateCRC(crc, u->fb[i], 8);
    }
    crc = updateCRC(crc, u->fb[i], u->fbpos&7);
    u->fb[4] = (crc>>8) & 0xff;
    u->fb[5] = crc & 0xff;
}


//*****************************************************************************
void packFrame(unpackmp2_t* u) {
    int i,j,q;
    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);    //initialize fbgetbits = skip header and crc
    memset(u->fb+(u->fbpos>>3), 0, sizeof(u->fb)-(u->fbpos>>3));
    //pack bit allocations
    const char* allocBits = ALLOCTAB_BITS[u->allocTabNum >> 1];
    for (i=0; i < u->sbLimit; i++) {
        for (j=0; j < ((i<u->jsBound)?2:1); j++) {
            fbputbits(u, u->bitalloc2BITS[j][i], allocBits[i]);    //put max. 4 bits
        }
    }
    //pack scalefactor selection info
    for (i=0; i < u->sbLimit; i++) {
        for (j=0; j < u->numChannels; j++) {
            if (u->bitalloc2[j][i] != NULL) { fbputbits(u, u->scfsiBITS[j][i], 2); }
        }
    }
    writeHeaderCRC(u);
    //pack scalefactors
    for (i=0; i < u->sbLimit; i++) {
        for (j=0; j < u->numChannels; j++) {
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
    //pack samples
    for (q=0; q < 36; q += 3) {
        for (i=0; i < u->sbLimit; i++) {
            for (j=0; j < ((i<u->jsBound)?2:1); j++) {
                const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                if (bitalloc2 != NULL) {
                    fbputbits(u, u->sampleBITS[j][q][i], bitalloc2->bits);    //put max. 16 bits
                    if (bitalloc2->steps == 0) {
                        fbputbits(u, u->sampleBITS[j][q+1][i], bitalloc2->bits);    //put max. 16 bits
                        fbputbits(u, u->sampleBITS[j][q+2][i], bitalloc2->bits);    //put max. 16 bits
                    }
                }
            }
        }
    }
}


//*****************************************************************************
int pack(FILE* infile, FILE* outfile) {
    //check um2 file header
    if ((getc(infile)!='u') || (getc(infile)!='m') || (getc(infile)!='2') || (getc(infile)!='\1')) {
        fprintf(stderr, "NOT AN UNPACKED MP2 FILE. (missing um2 file header)\n");
        return 5;
    }
    //pack frames
    int framecount = 0;
    for (;;) {
        int framesInBlock = 0;
        //----- read a block of frames from input file -----
        framesInBlock = (getc(infile)<<8) | getc(infile);
        if ((framesInBlock == 0) || (feof(infile))) {   //no more input data
            return 0;   //EOF (expected)
        }
        if (framesInBlock > MAX_FRAMES_PER_BLOCK) {
            fprintf(stderr, "error: frames_in_block(%d) is greater than MAX_FRAMES_PER_BLOCK(%d)\n", framesInBlock, MAX_FRAMES_PER_BLOCK);
            return 5;
        }
        int frm,b,i,j,q,bits;
        //read frame headers
        //read bit allocations
        for (i=0; i < MAX_SBLIMIT; i++) {
            for (frm=0; frm<framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i == 0) {
                    b = getc(infile);
                    if (b == EOF) break;
                    if (b == 0xFF) {    //frame header is stored -> read it from file
                        for (j=0; j<4; j++) { u->fb[j] = b;  b = getc(infile); }
                    } else {    //frame header is not stored -> copy from previous frame
                        if (frm == 0) {
                            fprintf(stderr, "error: missing first frame header in block! (corrupted um2 file or bug?)\n");
                            return 5;
                        }
                        for (j=0; j<4; j++) { u->fb[j] = (u-1)->fb[j]; }
                    }
                    b = ungetc(b, infile);
                    if (b == EOF) break;
                    extractFrameHeaderInfo(u);  //printFrameHeaderInfo(u,framecount);
                    framecount++;
                }
                if (i < u->sbLimit) {
                    const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
                    b = getc(infile);
                    if (b == EOF) break;
                    u->bitalloc2BITS[0][i] = b&0x0F;
                    u->bitalloc2[0][i] = alloc[i][b&0x0F];
                    if (i < u->jsBound) {
                        u->bitalloc2BITS[1][i] = b>>4;
                        u->bitalloc2[1][i] = alloc[i][b>>4];
                    } else {
                        u->bitalloc2[1][i] = u->bitalloc2[0][i];
                    }
                }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading frame headers and bit allocations! (corrupted um2 file or bug?)\n");
            return 5;   //unexpected EOF
        }
        //read scalefactor selection info
        //read scalefactors
        for (i=0; i < MAX_SBLIMIT; i++) {
            for (frm=0; frm<framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) { for (j=0; j < u->numChannels; j++) {
                    if (u->bitalloc2[j][i] != NULL) {
                        b = getc(infile);
                        if (b == EOF) break;
                        u->scfsiBITS[j][i] = (b>>6)&3;
                        u->scaleBITS[j][0][i] = b&0x3F;
                        switch (u->scfsiBITS[j][i]) {
                        case 0:
                            b = getc(infile);
                            u->scaleBITS[j][1][i] = b&0x3F;
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
                } }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading scalefactors! (corrupted um2 file or bug?)\n");
            return 5;   //unexpected EOF
        }
        //read samples
        for (bits=3; bits <= 16; bits++) {
            for (i=0; i < MAX_SBLIMIT; i++) {
                for (frm=0; frm<framesInBlock; frm++) {
                    unpackmp2_t* u = &UM2_ARRAY[frm];
                    if (i < u->sbLimit) { for (j=0; j < ((i<u->jsBound)?2:1); j++) {
                        const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                        if ((bitalloc2 != NULL) && (bitalloc2->bits == bits)) {
                            for (q=0; q < 36; q +=3) {
                                u->sampleBITS[j][q][i] = getc(infile);
                                if (bitalloc2->bits > 8) { u->sampleBITS[j][q][i] |= getc(infile)<<8; }
                                if (bitalloc2->steps == 0) {
                                    u->sampleBITS[j][q+1][i] = getc(infile);
                                    if (bitalloc2->bits > 8) { u->sampleBITS[j][q+1][i] |= getc(infile)<<8; }
                                    u->sampleBITS[j][q+2][i] = getc(infile);
                                    if (bitalloc2->bits > 8) { u->sampleBITS[j][q+2][i] |= getc(infile)<<8; }
                                }
                            }
                        }
                    } }
                }
            }
        }
        if (feof(infile)) {
            fprintf(stderr, "error: EOF while reading samples! (corrupted um2 file or bug?)\n");
            return 5;   //unexpected EOF
        }
        if (ferror(infile)) {
            perror("read unpacked frames");
            return 5;   //error
        }
        //----- write packed data to output file -----
        for (frm=0; frm<framesInBlock; frm++) {
            unpackmp2_t* u = &UM2_ARRAY[frm];
            packFrame(u);
            if ((fwrite(u->fb, 1, u->hdrLength, outfile) != (unsigned)(u->hdrLength)) || ferror(outfile)) {
                perror("write mp2 frame");
                return 5;
            }
        }
fprintf(stderr, "packed um2 frames: %d\n", framecount);
    }
    //NOTREACHED
    return 0;
}


//*****************************************************************************
void unpackFrame(unpackmp2_t* u) {
    int i,j,q;
    u->fbpos = 32 + (u->hdrHasCrc ? 16 : 0);    //initialize fbgetbits = skip header and crc
    //unpack bit allocations
    const sballoc_t*** alloc = ALLOCTABS[u->allocTabNum >> 1];
    const char* allocBits = ALLOCTAB_BITS[u->allocTabNum >> 1];
    for (i=0; i < u->sbLimit; i++) {
        for (j=0; j < ((i<u->jsBound)?2:1); j++) {
            int bits = fbgetbits(u, allocBits[i]);    //get max. 4 bits
            u->bitalloc2BITS[j][i] = bits;
            u->bitalloc2[j][i] = alloc[i][bits];
            if (i >= u->jsBound) { u->bitalloc2[1][i] = u->bitalloc2[0][i]; }
        }
    }
    //unpack scalefactor selection info
    for (i=0; i < u->sbLimit; i++) {
        for (j=0; j < u->numChannels; j++) {
            if (u->bitalloc2[j][i] != NULL) { u->scfsiBITS[j][i] = fbgetbits(u, 2); }
        }
    }
    //unpack scalefactors
    for (i=0; i < u->sbLimit; i++) {
        for (j=0; j < u->numChannels; j++) {
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
    //unpack samples
    for (q=0; q < 36; q += 3) {
        for (i=0; i < u->sbLimit; i++) {
            for (j=0; j < ((i<u->jsBound)?2:1); j++) {
                const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                if (bitalloc2 != NULL) {
                    u->sampleBITS[j][q][i] = fbgetbits(u, bitalloc2->bits);    //get max. 16 bits
                    if (bitalloc2->steps == 0) {
                        u->sampleBITS[j][q+1][i] = fbgetbits(u, bitalloc2->bits);    //get max. 16 bits
                        u->sampleBITS[j][q+2][i] = fbgetbits(u, bitalloc2->bits);    //get max. 16 bits
                    }
                }
            }
        }
    }
}


//*****************************************************************************
int unpack(FILE* infile, FILE* outfile) {
    const int MAX_SYNC_SKIP_BYTES = 512*1024;
    int framecount = 0;
    //process all frames
    for (;;) {
        //----- read a block of frames from input file -----
        int framesInBlock = 0;
        for (framesInBlock=0; framesInBlock<MAX_FRAMES_PER_BLOCK; framesInBlock++) {
            unpackmp2_t* u = &UM2_ARRAY[framesInBlock];
            int skipped = 0;
            int b0 = getc(infile);
            int b1 = getc(infile);
            int b2 = getc(infile);
            int b3 = EOF;
            // check um2 file header
            if ((framecount==0) && (b0=='u') && (b1=='m') && (b2=='2')) {
                fprintf(stderr, "NOT AN MP2 FILE. (found um2 file header)\n");
                return 4;   //error - input file is unpacked
            }
            // sync to frame header
            while (skipped <= MAX_SYNC_SKIP_BYTES) {
                b3 = getc(infile);
                if ((b0 == EOF) || (b1 == EOF) || (b2 == EOF) || (b3 == EOF)) {
                    if (framecount == 0) {
                        fprintf(stderr, "NOT AN MP2 FILE. (sync frame header, skipped %d bytes, EOF.)\n", skipped);
                        return 4;
                    } else { break; }    //EOF
                } else {
                    if ( (b0 == 0xFF) &&
                         (((b1&0xFE) == 0xFC) || ((b1&0xFE) == 0xF4)) &&    //MPEG-1 or MPEG-2 Layer2
                         ((b2&0xF0) != 0x00) && //bitrate != free
                         ((b2&0xF0) != 0xF0) && //bitrate != bad
                         ((b2&0x0C) != 0x0C)    //frequency != reserved
                    ) {
                        break;      //OK = frame sync!
                    } else {
                        skipped++;  //no frame sync
                        b0 = b1;
                        b1 = b2;
                        b2 = b3;
                    }
                }
            }
            if (skipped >= MAX_SYNC_SKIP_BYTES) {
                if (framecount == 0) {
                    fprintf(stderr, "NOT AN MP2 FILE. (sync frame header, skipped %d bytes, no sync.)\n", skipped);
                    return 4;
                } else { break; }    //lost sync
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
            extractFrameHeaderInfo(u); //printFrameHeaderInfo(u,framecount);
            if ((fread(&u->fb[4], 1, u->hdrLength-4, infile) != (unsigned)(u->hdrLength-4)) || feof(infile)) {
                fprintf(stderr, "read mp2 frame: EOF.\n");
                break;
            } else if (ferror(infile)) {
                perror("read mp2 frame");
                break;
            }
            unpackFrame(u);
            ++framecount;
        }
        if (framesInBlock == 0) {   //end of input file exactly on MAX_FRAMES_PER_BLOCK boundary
            return 0;
        }
        //----- write unpacked data to output file -----
        if (framecount <= MAX_FRAMES_PER_BLOCK) {   //first block of frames
            putc('u', outfile); putc('m', outfile); putc('2', outfile); putc('\1', outfile);    //write um2 file header
        }
        putc(framesInBlock>>8, outfile);    //write number of frames in block
        putc(framesInBlock,    outfile);
        int frm,i,j,q,bits;
        //write frame headers
        //write bit allocations
        for (i=0; i < MAX_SBLIMIT; i++) {
            for (frm=0; frm<framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if ((i == 0) && ((frm == 0) || (memcmp((u-1)->fb, u->fb, 4) != 0))) { //write header only if nessecary
                    for (j=0; j < 4; j++) {
                        putc(u->fb[j], outfile);
                    }
                }
                if (i < u->sbLimit) { if (i < u->jsBound) {
                    putc((u->bitalloc2BITS[1][i]<<4) | u->bitalloc2BITS[0][i], outfile);
                } else {
                    putc(u->bitalloc2BITS[0][i], outfile);
                } }
            }
        }
        //write scalefactor selection info
        //write scalefactors
        for (i=0; i < MAX_SBLIMIT; i++) {
            for (frm=0; frm<framesInBlock; frm++) {
                unpackmp2_t* u = &UM2_ARRAY[frm];
                if (i < u->sbLimit) { for (j=0; j < u->numChannels; j++) {
                    if (u->bitalloc2[j][i] != NULL) {
                        putc((u->scfsiBITS[j][i]<<6)|u->scaleBITS[j][0][i], outfile);
                        switch (u->scfsiBITS[j][i]) {
                        case 0:
                            if (u->scaleBITS[j][0][i] == u->scaleBITS[j][2][i]) {
                                putc((1<<7)|u->scaleBITS[j][1][i], outfile);
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
                } }
            }
        }
        //write samples
        for (bits=3; bits <= 16; bits++) {
            for (i=0; i < MAX_SBLIMIT; i++) {
                for (frm=0; frm<framesInBlock; frm++) {
                    unpackmp2_t* u = &UM2_ARRAY[frm];
                    if (i < u->sbLimit) { for (j=0; j < ((i<u->jsBound)?2:1); j++) {
                        const sballoc_t* bitalloc2 = u->bitalloc2[j][i];
                        if ((bitalloc2 != NULL) && (bitalloc2->bits == bits)) {
                            for (q=0; q < 36; q += 3) {
                                putc(u->sampleBITS[j][q][i], outfile);
                                if (bitalloc2->bits > 8) { putc(u->sampleBITS[j][q][i] >> 8, outfile); }
                                if (bitalloc2->steps == 0) {
                                    putc(u->sampleBITS[j][q+1][i], outfile);
                                    if (bitalloc2->bits > 8) { putc(u->sampleBITS[j][q+1][i] >> 8, outfile); }
                                    putc(u->sampleBITS[j][q+2][i], outfile);
                                    if (bitalloc2->bits > 8) { putc(u->sampleBITS[j][q+2][i] >> 8, outfile); }
                                }
                            }
                        }
                    } }
                }
            }
        }
        if (ferror(outfile)) {
            perror("write unpacked frame");
            return 4;
        }
fprintf(stderr, "unpacked mp2 frames: %d\n", framecount);
    }
    //NOTREACHED
    return 0;
}


//*****************************************************************************
int main(int argc, char **argv) {
    int result = 0;
    if ((argc!=2) || ((argv[1][0]!='u') && (argv[1][0]!='p')) || (argv[1][1]!='\0')) {
        fprintf(stderr,
            "unpackmp2 v1.1, lossless MPEG audio Layer II transform, (C) 2009 Michael Henke\n"
            "This is free software under GNU GPL v3, http://www.gnu.org/copyleft/gpl.html\n"
            "\n"
            "unpack mp2 to um2:  unpackmp2 u  < input  > output\n"
            "pack um2 to mp2:    unpackmp2 p  < input  > output\n"
            "\n");
        exit(1);
    }
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);
    if (argv[1][0]=='p') {
        result = pack(stdin, stdout);
    } else if (argv[1][0]=='u') {
        result = unpack(stdin, stdout);
    }
    return result;
}
