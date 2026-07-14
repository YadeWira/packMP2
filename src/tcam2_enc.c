/*  TCAM2 encoder — zlib deflate on um2 data.
    Domain-aware preprocessing (scalefactor delta, etc.) pending.
    Copyright (C) 2026 Tovy. GPLv3.
*/

#include "tcam2.h"
#include <zlib.h>

int tcam2_compress(FILE *in, FILE *out) {
    /* Read all input (works with pipes) */
    long size = 0, cap = 65536;
    unsigned char *data = (unsigned char*)malloc(cap);
    if (!data) return 1;
    int c;
    while ((c = getc(in)) != EOF) {
        if (size >= cap) { cap *= 2; data = (unsigned char*)realloc(data, cap); }
        data[size++] = (unsigned char)c;
    }

    /* TCAM2 header: "TCAM2" + original_size (4 bytes BE) */
    putc('T', out); putc('C', out); putc('A', out); putc('M', out);
    putc('2', out);
    putc((size>>24)&0xFF, out); putc((size>>16)&0xFF, out);
    putc((size>> 8)&0xFF, out); putc( size     &0xFF, out);

    /* Deflate raw um2 data */
    z_stream z;
    z.zalloc = Z_NULL; z.zfree = Z_NULL; z.opaque = Z_NULL;
    deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);

    unsigned char zbuf[65536];
    z.next_in = data; z.avail_in = size;
    int ret;
    do {
        z.next_out = zbuf; z.avail_out = sizeof(zbuf);
        ret = deflate(&z, Z_FINISH);
        fwrite(zbuf, 1, sizeof(zbuf) - z.avail_out, out);
    } while (ret == Z_OK);
    deflateEnd(&z);

    free(data);
    fprintf(stderr, "TCAM2: %ld -> %ld bytes (%.1f%%)\n",
            size, ftell(out), 100.0 * ftell(out) / (size ? size : 1));
    return 0;
}
