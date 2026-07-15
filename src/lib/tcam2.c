/*  TCAM2 - Tovy Compresor de Audio MP2
    Domain-aware lossless codec for unpackmp2 um2 format.
    Usage:
      tcam2 c|d                    (stdin -> stdout, default)
      tcam2 c|d <input> <output>   (file mode)
    Copyright (C) 2026 Tovy. GPLv3.
*/

#include "tcam2.h"

int main(int argc, char **argv) {
    if (argc != 2 && argc != 4) goto usage;
    if (argv[1][0] != 'c' && argv[1][0] != 'd') goto usage;
    if (argv[1][1] != '\0') goto usage;

    FILE *in  = stdin;
    FILE *out = stdout;

#ifdef _WIN32
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);
#endif

    if (argc == 4) {
        in = fopen(argv[2], "rb");
        if (!in) { perror(argv[2]); return 1; }
        out = fopen(argv[3], "wb");
        if (!out) { perror(argv[3]); fclose(in); return 1; }
    }

    int rc;
    if (argv[1][0] == 'c')
        rc = tcam2_compress(in, out);
    else
        rc = tcam2_decompress(in, out);

    if (argc == 4) { fclose(in); fclose(out); }
    return rc;

usage:
    fprintf(stderr,
        "TCAM2 v1 - Tovy Compresor de Audio MP2\n"
        "Domain-aware lossless um2 compressor (pairs with unpackmp2)\n"
        "\n"
        "  compress:   tcam2 c < input.um2 > output.tcam2\n"
        "              tcam2 c input.um2 output.tcam2\n"
        "  decompress: tcam2 d < input.tcam2 > output.um2\n"
        "\n"
        "  pipeline:   unpackmp2 u < input.mp2 | tcam2 c > output.tcam2\n"
        "              tcam2 d < input.tcam2 | unpackmp2 p > output.mp2\n");
    return 1;
}
