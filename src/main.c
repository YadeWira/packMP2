/*  packMP2 — MPEG Audio Layer II lossless transform + compression
    Unified CLI: unpack, pack, compress, decompress, pipe.
    Copyright (C) 2009-2010 Michael Henke, 2026 Tovy. GPLv3.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
# include <fcntl.h>
# include <io.h>
# define SET_BINARY_MODE(f) _setmode(_fileno(f), _O_BINARY)
#else
# define SET_BINARY_MODE(f) ((void)0)
#endif

/* Forward declarations from library */
extern int unpack(FILE *in, FILE *out);
extern int pack(FILE *in, FILE *out);
extern int tcam2_compress(FILE *in, FILE *out);
extern int tcam2_decompress(FILE *in, FILE *out);

static void usage(void) {
    fprintf(stderr,
        "packMP2 — MPEG Audio Layer II lossless transform + compression\n"
        "Copyright (C) 2009-2010 Michael Henke, 2026 Tovy. GPLv3.\n"
        "\n"
        "  unpack      mp2 -> um2    packmp2 u < in.mp2 > out.um2\n"
        "  pack        um2 -> mp2    packmp2 p < in.um2 > out.mp2\n"
        "  compress    um2 -> tcam2  packmp2 c < in.um2 > out.tcam2\n"
        "  decompress  tcam2 -> um2  packmp2 d < in.tcam2 > out.um2\n"
        "  pipe        mp2 -> mp2    packmp2 x < in.mp2 > out.mp2\n"
        "\n"
        "  Subproject of packMP3 — https://github.com/YadeWira/packMP2\n");
}

int main(int argc, char **argv) {
    if (argc != 2 || argv[1][1] != '\0') { usage(); return 1; }

    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);

    char cmd = argv[1][0];
    switch (cmd) {
    case 'u': return unpack(stdin, stdout);
    case 'p': return pack(stdin, stdout);
    case 'c': return tcam2_compress(stdin, stdout);
    case 'd': return tcam2_decompress(stdin, stdout);
    case 'x': { /* pipe: unpack | compress | decompress | pack */
        /* This requires piping between stages. For now, exit with message. */
        fprintf(stderr, "pipe mode: use shell pipeline instead\n");
        fprintf(stderr, "  packmp2 u < in.mp2 | packmp2 c | packmp2 d | packmp2 p > out.mp2\n");
        return 1;
    }
    default: usage(); return 1;
    }
}
