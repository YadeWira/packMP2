/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

/* Extract up to 16 bits from the frame buffer, advancing the bit position. */
U32 fbgetbits(unpackmp2_t* u, int n) {
    U32 result = 0;
    if (n > 0) {
        result = ( (((U32)(u->fb[(u->fbpos>>3)+0])<<24) |
                    ((U32)(u->fb[(u->fbpos>>3)+1])<<16) |
                    ((U32)(u->fb[(u->fbpos>>3)+2])<< 8)) >> (32-(u->fbpos&7)-n) ) & ((1<<n)-1);
        u->fbpos += n;
    }
    return result;
}

/* Write up to 16 bits into the frame buffer, advancing the bit position. */
void fbputbits(unpackmp2_t* u, U32 v, int n) {
    if (n > 0) {
        v &= (1<<n)-1;
        v <<= 32-(u->fbpos&7)-n;
        u->fb[(u->fbpos>>3)+0] |= v>>24;
        u->fb[(u->fbpos>>3)+1]  = v>>16;
        u->fb[(u->fbpos>>3)+2]  = v>> 8;
        u->fbpos += n;
    }
}
