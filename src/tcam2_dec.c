/*  TCAM2 decoder — zlib inflate.
    Copyright (C) 2026 Tovy. GPLv3.
*/

#include "tcam2.h"
#include <zlib.h>

int tcam2_decompress(FILE *in, FILE *out) {
    if (getc(in)!='T'||getc(in)!='C'||getc(in)!='A'||getc(in)!='M'||getc(in)!='2') {
        fprintf(stderr, "TCAM2: bad header\n"); return 1;
    }
    long orig_size = ((long)getc(in)<<24)|((long)getc(in)<<16)|
                     ((long)getc(in)<< 8)|((long)getc(in)    );

    /* Read all compressed data (works with pipes) */
    long comp_size = 0, comp_cap = 65536;
    unsigned char *comp = (unsigned char*)malloc(comp_cap);
    if (!comp) return 1;
    int c2;
    while ((c2 = getc(in)) != EOF) {
        if (comp_size >= comp_cap) {
            comp_cap *= 2;
            comp = (unsigned char*)realloc(comp, comp_cap);
            if (!comp) return 1;
        }
        comp[comp_size++] = (unsigned char)c2;
    }

    unsigned char *data = (unsigned char*)malloc(orig_size);
    if (!data) { free(comp); return 1; }

    z_stream z;
    z.zalloc = Z_NULL; z.zfree = Z_NULL; z.opaque = Z_NULL;
    z.next_in = comp; z.avail_in = comp_size;
    z.next_out = data; z.avail_out = orig_size;
    inflateInit2(&z, -15);
    if (inflate(&z, Z_FINISH) != Z_STREAM_END) { free(comp); free(data); return 1; }
    inflateEnd(&z); free(comp);

    fwrite(data, 1, orig_size, out);
    free(data);
    return 0;
}
