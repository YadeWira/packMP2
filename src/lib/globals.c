/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

/* Global frame buffer array */
unpackmp2_t UM2_ARRAY[MAX_FRAMES_PER_BLOCK];

/* Suppress informational stderr output from unpack/pack */
int unpackmp2_quiet = 0;

/* MPEG frame header lookup tables */
const char  FRMHDR_LSF[4]        = {2, 3, 1, 0};
const int   FRMHDR_FREQUENCY[4]  = {44100, 48000, 32000, -1};
const short FRMHDR_BITRATE[2][16] = {
    {-1, 32, 48, 56, 64, 80, 96,112,128,160,192,224,256,320,384,-1},    /* MPEG-1 LayerII */
    {-1,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160,-1}     /* MPEG-2 LSF LayerII */
};

/* Bit allocation tables */
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
