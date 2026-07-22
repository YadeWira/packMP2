/*  um2_delta — CLI tool for inter-frame delta compression of um2 files.
    Usage:
      um2_delta e [keyframe_interval] < input.um2 > output_d.um2
      um2_delta d [keyframe_interval] < input_d.um2 > output.um2
      um2_delta e 64 < input.um2 > output_d.um2   (keyframe every 64 frames)
      um2_delta d 64 < input_d.um2 > output.um2

    Copyright (C) 2026 Tovy / packMP2 contributors. GPLv3.
*/

#include "um2_delta.h"
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2 || (argv[1][0] != 'e' && argv[1][0] != 'd')) {
        fprintf(stderr,
            "um2_delta v1 — Inter-frame delta compression for um2\n"
            "\n"
            "  encode: um2_delta e [keyframe_interval] < in.um2 > out_d.um2\n"
            "  decode: um2_delta d [keyframe_interval] < in_d.um2 > out.um2\n"
            "\n"
            "  keyframe_interval: 8, 16, 32, or 64 (default: 64)\n"
            "\n"
            "  Pipeline example:\n"
            "  unpackmp2 u < audio.mp2 | um2_delta e | tcam2 c > audio.pmp2\n"
            "  tcam2 d < audio.pmp2 | um2_delta d | packmp2 p > audio.mp2\n");
        return 1;
    }

    int keyframe_interval = 64;
    if (argc >= 3) {
        keyframe_interval = atoi(argv[2]);
        if (keyframe_interval <= 0) keyframe_interval = 64;
    }

#ifdef _WIN32
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);
#endif

    int rc;
    if (argv[1][0] == 'e')
        rc = um2_delta_enc_file(stdin, stdout, keyframe_interval);
    else
        rc = um2_delta_dec_file(stdin, stdout);

    return rc;
}
