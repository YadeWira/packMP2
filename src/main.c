/*  unpackmp2 - lossless transformation of MPEG audio Layer II data
    Copyright (C) 2009 Michael Henke
    See unpackmp2.h / GPLv3 for license details.

    The included Windows executable was compiled with MinGW (gcc 4.4.0):
    gcc unpackmp2.c -o unpackmp2 -Wall -O2 -fomit-frame-pointer -march=i686 -static-libgcc -s

    Changes:
    v1.0 (2009-10-04)
     - initial release

    v1.0.1 (2009-11-10)
     - added MPEG frame protection (CRC)

    v1.1 (2009-12-04)
     - improved compression by re-ordering the unpacked data
       (format of unpacked files is not compatible with previous version!)

    v1.2 (2026-07-14)
     - byte-exact roundtrip: preserve preamble, per-frame gaps, and trailer
       (um2 v2 format; backward compatible reader for v1)
*/

#include "unpackmp2.h"

int main(int argc, char **argv) {
    int result = 0;

    if ((argc != 2) || ((argv[1][0] != 'u') && (argv[1][0] != 'p')) || (argv[1][1] != '\0')) {
        fprintf(stderr,
            "unpackmp2 v1.2, lossless MPEG audio Layer II transform, (C) 2009 Michael Henke\n"
            "This is free software under GNU GPL v3, http://www.gnu.org/copyleft/gpl.html\n"
            "\n"
            "unpack mp2 to um2:  unpackmp2 u  < input  > output\n"
            "pack um2 to mp2:    unpackmp2 p  < input  > output\n"
            "\n");
        exit(1);
    }

    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);

    if (argv[1][0] == 'p') {
        result = pack(stdin, stdout);
    } else if (argv[1][0] == 'u') {
        result = unpack(stdin, stdout);
    }

    return result;
}
