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
*/

#ifndef UNPACKMP2_H
#define UNPACKMP2_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <fcntl.h>
# include <io.h>
# define SET_BINARY_MODE(file) _setmode(_fileno(file), _O_BINARY)
#else
# define SET_BINARY_MODE(file) ((void)0)
#endif

typedef unsigned int U32;   /* 32 bit unsigned integer */

#define MAX_SBLIMIT 30

#define UM2_VERSION  '\2'  /* um2 file format version (v1.2: byte-exact) */

/* MPEG frame header lookup tables */
extern const char  FRMHDR_LSF[4];
extern const int   FRMHDR_FREQUENCY[4];
extern const short FRMHDR_BITRATE[2][16];

/* Bit allocation tables */
typedef struct {
    char steps;
    char bits;
} sballoc_t;

extern const sballoc_t  ALLOC[17];
extern const sballoc_t* ATAB0[16];
extern const sballoc_t* ATAB1[16];
extern const sballoc_t* ATAB2[8];
extern const sballoc_t* ATAB3[4];
extern const sballoc_t* ATAB4[16];
extern const sballoc_t* ATAB5[16];
extern const sballoc_t** ALLOCTABS[3][MAX_SBLIMIT];
extern const char       ALLOCTAB_LENGTHS[5];
extern const char       ALLOCTAB_BITS[3][MAX_SBLIMIT];

/* Frame state */
typedef struct {
    unsigned char fb[1728+64];  /* frame buffer, max. frame length: 384kbps/32kHz + safety margin */
    unsigned int  fbpos;         /* bit position (used by functions fbgetbits / fbputbits) */
    unsigned int  hdrFrequency;  /* translated using FRMHDR_FREQUENCY: (44100/48000/32000) MPEG-1, (22050/24000/16000) MPEG-2 */
    const sballoc_t* bitalloc2[2][MAX_SBLIMIT];
    unsigned char   bitalloc2BITS[2][MAX_SBLIMIT];     /* values have 2...4 bits */
    unsigned char   scfsiBITS[2][MAX_SBLIMIT];         /* values have     2 bits */
    unsigned char   scaleBITS[2][3][MAX_SBLIMIT];      /* values have     6 bits */
    unsigned short  sampleBITS[2][36][MAX_SBLIMIT];    /* values have ...16 bits */
    unsigned short  hdrLength;    /* calculated using hdrBitrate and hdrFrequency, in Bytes */
    unsigned short  hdrBitrate;   /* translated using FRMHDR_BITRATE: (32...384) MPEG-1, (8...160) MPEG-2 */
    unsigned char   hdrLsf;       /* translated using FRMHDR_LSF: 0=MPEG-1, 1=MPEG-2 */
    unsigned char   hdrMode;      /* directly from header: 0=Stereo, 1=Joint stereo, 2=Dual channel, 3=Single channel (Mono) */
    unsigned char   hdrModeExt;   /* directly from header: use intensity stereo for bands: 0=4...31, 1=8...31, 2=12...31, 3=16...31 */
    unsigned char   hdrHasCrc;    /* directly from header (inverted): true=frame has CRC */
    unsigned char   numChannels;  /* number of channels, calculated using hdrMode: 1=Mono, 2=Dual/Joint/Stereo */
    unsigned char   allocTabNum;  /* number of the bit allocation table */
    unsigned char   sbLimit;      /* scalefactor band limit, translated using allocTabNum in ALLOCTAB_LENGTHS */
    unsigned char   jsBound;      /* joint stereo boundary, calculated using hdrMode and hdrModeExt or sbLimit */
} unpackmp2_t;

/* MAX_FRAMES_PER_BLOCK must be < 65536 (16 bits) */
#define MAX_FRAMES_PER_BLOCK  4096
extern unpackmp2_t UM2_ARRAY[MAX_FRAMES_PER_BLOCK];

/* bitio.c */
U32  fbgetbits(unpackmp2_t* u, int n);
void fbputbits(unpackmp2_t* u, U32 v, int n);

/* frame.c */
void extractFrameHeaderInfo(unpackmp2_t* u);
void printFrameHeaderInfo(unpackmp2_t* u, int frameNumber);
U32  updateCRC(U32 crc, unsigned char value, int n);
void writeHeaderCRC(unpackmp2_t* u);

/* pack.c */
void packFrame(unpackmp2_t* u);
int  pack(FILE* infile, FILE* outfile);

/* unpack.c */
void unpackFrame(unpackmp2_t* u);
int  unpack(FILE* infile, FILE* outfile);

#endif /* UNPACKMP2_H */
