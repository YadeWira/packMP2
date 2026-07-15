/*  packMP2 — MPEG Audio Layer II lossless transform + compression
    Unified CLI with switches for testing. v0.2
    Copyright (C) 2009-2010 Michael Henke, 2026 Tovy. GPLv3.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
# include <fcntl.h>
# include <io.h>
# define SET_BINARY_MODE(f) _setmode(_fileno(f), _O_BINARY)
#else
# define SET_BINARY_MODE(f) ((void)0)
#endif

#define VERSION "0.2"

/* Forward declarations */
extern int unpack(FILE *in, FILE *out);
extern int pack(FILE *in, FILE *out);
extern int tcam2_compress(FILE *in, FILE *out, int level);
extern int tcam2_decompress(FILE *in, FILE *out);
extern int tcam2_quiet;

/* Weak stubs for lite build (no zstd) */
__attribute__((weak)) int tcam2_compress(FILE *in, FILE *out, int level) {
    (void)in;(void)out;(void)level;
    fprintf(stderr,"TCAM2 not available (lite build). Use --raw for passthrough.\n");
    return 1;
}
__attribute__((weak)) int tcam2_decompress(FILE *in, FILE *out) {
    (void)in;(void)out;
    fprintf(stderr,"TCAM2 not available (lite build). Use --raw for passthrough.\n");
    return 1;
}
__attribute__((weak)) int tcam2_quiet = 0;

static void print_help(void) {
    fprintf(stderr,
        "packMP2 v" VERSION " — MPEG Audio Layer II lossless transform + compression\n"
        "Sub-project of packMP3. https://github.com/YadeWira/packMP2\n"
        "\n"
        "  packmp2 <command> [options]\n"
        "\n"
        "Commands:\n"
        "  u, unpack      mp2 -> um2\n"
        "  p, pack        um2 -> mp2\n"
        "  c, compress    um2 -> tcam2\n"
        "  d, decompress  tcam2 -> um2\n"
        "  x, pipe        mp2 -> um2 -> tcam2 -> um2 -> mp2\n"
        "\n"
        "Options:\n"
        "  -i, --input F   Read from file (default: stdin)\n"
        "  -o, --output F  Write to file (default: stdout)\n"
        "  -q, --quiet     Suppress progress messages\n"
        "  -l, --level N   zstd level 1-9 (default: 1)\n"
        "  --raw           c/d passthrough, no compression (testing)\n"
        "  -b, --benchmark Report timing + ratio\n"
        "  --verify        After pipe, verify byte-exact roundtrip\n"
        "  -V, --version   Print version\n"
        "  -h, --help      This help\n"
        "\n"
        "Examples:\n"
        "  packmp2 u -i in.mp2 -o out.um2 -q\n"
        "  packmp2 c -i in.um2 -o out.tcam2 -l 3 -b\n"
        "  packmp2 x -i in.mp2 -o out.mp2 --verify -b\n");
}

int main(int argc, char **argv) {
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);

    /* Parse command */
    if (argc < 2) { print_help(); return 1; }

    char cmd = 0;
    int arg_start = 1;
    if (strcmp(argv[1], "u") == 0 || strcmp(argv[1], "unpack") == 0)      cmd = 'u';
    else if (strcmp(argv[1], "p") == 0 || strcmp(argv[1], "pack") == 0)   cmd = 'p';
    else if (strcmp(argv[1], "c") == 0 || strcmp(argv[1], "compress") == 0) cmd = 'c';
    else if (strcmp(argv[1], "d") == 0 || strcmp(argv[1], "decompress") == 0) cmd = 'd';
    else if (strcmp(argv[1], "x") == 0 || strcmp(argv[1], "pipe") == 0)   cmd = 'x';
    else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) { print_help(); return 0; }
    else if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
        fprintf(stderr, "packMP2 v" VERSION "\n"); return 0;
    }
    else { arg_start = 0; cmd = '?'; } /* no command found, check first arg for flags */

    if (!cmd) { print_help(); return 1; }

    /* Parse options */
    char *in_file = NULL, *out_file = NULL;
    int quiet = 0, benchmark = 0, verify = 0, raw = 0, level = 1;

    for (int i = arg_start + 1; i < argc; i++) {
        if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0)      quiet = 1;
        else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--benchmark") == 0) benchmark = 1;
        else if (strcmp(argv[i], "--verify") == 0)                                verify = 1;
        else if (strcmp(argv[i], "--raw") == 0)                                   raw = 1;
        else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--version") == 0)
            { fprintf(stderr, "packMP2 v" VERSION "\n"); return 0; }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            { print_help(); return 0; }
        else if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) && i+1 < argc)
            in_file = argv[++i];
        else if ((strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) && i+1 < argc)
            out_file = argv[++i];
        else if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0) && i+1 < argc)
            level = atoi(argv[++i]);
        else {
            fprintf(stderr, "packMP2: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    tcam2_quiet = quiet;

    /* Open files */
    FILE *in = stdin, *out = stdout;
    if (in_file)  { in  = fopen(in_file,  "rb"); if (!in)  { perror(in_file);  return 1; } }
    if (out_file) { out = fopen(out_file, "wb"); if (!out) { perror(out_file); return 1; } }

    /* Execute command */
    clock_t t0 = clock();
    int rc = 0;

    switch (cmd) {
    case 'u':
        if (!quiet) fprintf(stderr, "packMP2: unpacking...\n");
        rc = unpack(in, out);
        break;
    case 'p':
        if (!quiet) fprintf(stderr, "packMP2: packing...\n");
        rc = pack(in, out);
        break;
    case 'c':
        if (raw) {
            if (!quiet) fprintf(stderr, "packMP2: raw copy (no compression)...\n");
            int c; while ((c = getc(in)) != EOF) putc(c, out);
        } else {
            if (!quiet) fprintf(stderr, "packMP2: compressing (level %d)...\n", level);
            rc = tcam2_compress(in, out, level);
        }
        break;
    case 'd':
        if (raw) {
            if (!quiet) fprintf(stderr, "packMP2: raw copy (no decompression)...\n");
            int c; while ((c = getc(in)) != EOF) putc(c, out);
        } else {
            if (!quiet) fprintf(stderr, "packMP2: decompressing...\n");
            rc = tcam2_decompress(in, out);
        }
        break;
    case 'x': {
        /* Pipe: unpack -> compress -> decompress -> pack */
        /* Use temporary files for simplicity */
        if (!quiet) fprintf(stderr, "packMP2: pipeline (unpack -> compress -> decompress -> pack)...\n");

        /* Read all input */
        long isz = 0, icap = 65536;
        unsigned char *idata = malloc(icap);
        if (!idata) { rc = 1; break; }
        int c; while ((c = getc(in)) != EOF) {
            if (isz >= icap) { icap *= 2; idata = realloc(idata, icap); }
            idata[isz++] = (unsigned char)c;
        }

        /* Phase 1: unpack mp2 -> um2 (write input to temp file first) */
        FILE *mp2_f = tmpfile(); if (!mp2_f) { free(idata); rc = 1; break; }
        fwrite(idata, 1, isz, mp2_f); rewind(mp2_f);
        FILE *um2_f = tmpfile(); if (!um2_f) { fclose(mp2_f); free(idata); rc = 1; break; }
        t0 = clock();
        rc = unpack(mp2_f, um2_f);
        fclose(mp2_f);
        if (rc) { fclose(um2_f); free(idata); break; }

        /* Phase 2: compress um2 -> tcam2 (in memory) */
        rewind(um2_f);
        FILE *tc_f = tmpfile(); if (!tc_f) { fclose(um2_f); free(idata); rc = 1; break; }
        rc = tcam2_compress(um2_f, tc_f, level);
        fclose(um2_f);
        if (rc) { fclose(tc_f); free(idata); break; }

        /* Phase 3: decompress tcam2 -> um2 (in memory) */
        rewind(tc_f);
        FILE *um2_r = tmpfile(); if (!um2_r) { fclose(tc_f); free(idata); rc = 1; break; }
        rc = tcam2_decompress(tc_f, um2_r);
        fclose(tc_f);
        if (rc) { fclose(um2_r); free(idata); break; }

        /* Phase 4: pack um2 -> mp2 */
        rewind(um2_r);
        rc = pack(um2_r, out);
        fclose(um2_r);

        /* Verify if requested */
        if (verify && rc == 0 && out_file) {
            fclose(out); out = NULL; /* close so we can reopen for reading */
            long osz = isz; /* use input size */
            FILE *vrf = fopen(out_file, "rb");
            if (!vrf) { perror(out_file); rc = 1; }
            else {
                fseek(vrf, 0, SEEK_END);
                osz = ftell(vrf);
                rewind(vrf);
                unsigned char *odata = malloc(osz);
                fread(odata, 1, osz, vrf);
                fclose(vrf);
                if (osz == isz && memcmp(idata, odata, isz) == 0)
                    fprintf(stderr, "verify: BYTE-EXACT OK\n");
                else {
                    fprintf(stderr, "verify: MISMATCH (in=%ld out=%ld)\n", isz, osz);
                    rc = 1;
                }
                free(odata);
            }
        }
        free(idata);
        break;
    }
    default:
        print_help();
        rc = 1;
    }

    double elapsed = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (benchmark && rc == 0) {
        fprintf(stderr, "  time: %.3fs  output: %ld bytes\n", elapsed, ftell(out));
    }

    if (in_file)  fclose(in);
    if (out_file && out) fclose(out);
    return rc;
}
