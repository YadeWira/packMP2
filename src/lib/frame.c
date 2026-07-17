/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009 Michael Henke
    See unpackmp2.h / GPLv3 for license details.
*/

#include "unpackmp2.h"

/* Parse frame header bytes and populate the unpackmp2_t metadata fields. */
void extractFrameHeaderInfo(unpackmp2_t* u) {
    u->hdrLsf      = FRMHDR_LSF[(u->fb[1] & 0x18)>>3];
    u->hdrLayer    = 4 - ((u->fb[1] & 0x06)>>1);  /* 11=I, 10=II, 01=III */
    u->hdrFrequency = FRMHDR_FREQUENCY[(u->fb[2] & 0x0C)>>2] >> u->hdrLsf;

    if (u->hdrLayer == 1) {
        /* Layer I: different bitrate table + frame length formula */
        u->hdrBitrate = FRMHDR_BITRATE_L1[(u->hdrLsf==0 ? 0 : 1)][(u->fb[2] & 0xF0)>>4];
        /* Layer I: 384 samples/frame, slot=4B.
           ISO formula: N = 12*bitrate*1000/freq (slots), bytes = (N+pad)*4.
           Compute as ((12000*br)/freq + pad) * 4 to preserve integer truncation. */
        u->hdrLength = ((12000 * u->hdrBitrate) / u->hdrFrequency + ((u->fb[2] & 0x02)>>1)) * 4;
        /* Layer I: all subbands available (up to 32, capped at MAX_SBLIMIT=30) */
        u->sbLimit = 30;  /* TODO: expand MAX_SBLIMIT to 32 for full Layer I */
        u->jsBound = u->sbLimit;
        u->jsBound = u->sbLimit;
    } else {
        u->hdrBitrate = FRMHDR_BITRATE[(u->hdrLsf==0 ? 0 : 1)][(u->fb[2] & 0xF0)>>4];
        /* Layer II: 1152 samples/frame, slot=1B. 144000 = 36*1000*1 */
        u->hdrLength = 144000 * u->hdrBitrate / u->hdrFrequency + ((u->fb[2] & 0x02)>>1);
    }

    u->hdrMode      = (u->fb[3] & 0xC0)>>6;
    u->hdrModeExt   = (u->fb[3] & 0x30)>>4;
    u->hdrHasCrc    = ((u->fb[1] & 0x01) == 0);
    u->numChannels  = (u->hdrMode==3) ? 1 : 2;

    int brpch = u->hdrBitrate / u->numChannels;
    if (u->hdrLsf != 0)        u->allocTabNum = 4;
    else if (brpch <= 48)      u->allocTabNum = (u->hdrFrequency == 32000) ? 3 : 2;
    else if (brpch <= 80)      u->allocTabNum = 0;
    else                       u->allocTabNum = (u->hdrFrequency == 48000) ? 0 : 1;

    u->sbLimit = ALLOCTAB_LENGTHS[(int)u->allocTabNum];
    u->jsBound = u->sbLimit;
    if (u->hdrMode == 1)       u->jsBound = (u->hdrModeExt + 1) * 4;
    else if (u->hdrMode == 3)  u->jsBound = 0;
}

/* Print frame header info to stderr (debug). */
void printFrameHeaderInfo(unpackmp2_t* u, int frameNumber) {
    fprintf(stderr,
        " frame(%d) : lsf(%d) freq(%d) bitrate(%d) framelength(%d) mode(%d) modeExt(%d) crc(%d) sblimit(%d) jsbound(%d)\n",
        frameNumber, u->hdrLsf, u->hdrFrequency, u->hdrBitrate, u->hdrLength,
        u->hdrMode, u->hdrModeExt, u->hdrHasCrc, u->sbLimit, u->jsBound);
}

/* Update CRC-16/Modbus with n bits of value. */
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

/* Compute and write the CRC-16 into the frame buffer (if CRC is enabled). */
void writeHeaderCRC(unpackmp2_t* u) {
    if ( ! u->hdrHasCrc ) return;
    U32 crc = 0xffff;
    crc = updateCRC(crc, u->fb[2], 8);
    crc = updateCRC(crc, u->fb[3], 8);
    int i;
    for (i = 4+2; i < u->fbpos>>3; i++) {
        crc = updateCRC(crc, u->fb[i], 8);
    }
    crc = updateCRC(crc, u->fb[i], u->fbpos&7);
    u->fb[4] = (crc>>8) & 0xff;
    u->fb[5] = crc & 0xff;
}
