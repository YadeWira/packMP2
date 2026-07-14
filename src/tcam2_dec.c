/*  TCAM2 decoder — zlib inflate + reverse byte-delta filter.
    Copyright (C) 2026 Tovy. GPLv3.
*/

#include "tcam2.h"
#include <zlib.h>

int tcam2_decompress(FILE *in, FILE *out) {
    if (getc(in) != 'T' || getc(in) != 'C' || getc(in) != 'A' ||
        getc(in) != 'M' || getc(in) != '2') {
        fprintf(stderr, "TCAM2: bad header\n"); return 1;
    }
    long orig_size = ((long)getc(in)<<24) | ((long)getc(in)<<16) |
                     ((long)getc(in)<< 8) | ((long)getc(in)    );

    /* Read compressed data */
    fseek(in, 0, SEEK_END);
    long comp_size = ftell(in) - 9;
    fseek(in, 9, SEEK_SET);
    unsigned char *comp = (unsigned char*)malloc(comp_size);
    if (!comp) return 1;
    fread(comp, 1, comp_size, in);

    /* Inflate */
    unsigned char *filtered = (unsigned char*)malloc(orig_size);
    if (!filtered) { free(comp); return 1; }
    z_stream z;
    z.zalloc = Z_NULL; z.zfree = Z_NULL; z.opaque = Z_NULL;
    z.next_in = comp; z.avail_in = comp_size;
    z.next_out = filtered; z.avail_out = orig_size;
    inflateInit2(&z, -15);
    int ret = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    free(comp);
    if (ret != Z_STREAM_END) { free(filtered); return 1; }

    fwrite(filtered, 1, orig_size, out);
    free(filtered);
    return 0;
}
