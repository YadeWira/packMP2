/* TCAM2 encoder — calls pack_preprocess() (proven read logic) + zstd.
   Copyright (C) 2026 Tovy. GPLv3. */
#include "unpackmp2.h"
#include <zstd.h>
#include "tcam2_dict.h"

int tcam2_compress(FILE *in, FILE *out) {
    /* Use pack_preprocess() — proven correct, same read logic as pack() */
    unsigned char *pp = NULL;
    long pp_size = 0;
    int rc = pack_preprocess(in, &pp, &pp_size);
    if (rc != 0 || !pp) { free(pp); return 1; }

    /* Write TCAM2 header */
    putc('T',out);putc('C',out);putc('A',out);putc('M',out);putc('2',out);
    putc((pp_size>>24)&0xFF,out);putc((pp_size>>16)&0xFF,out);
    putc((pp_size>>8)&0xFF,out);putc(pp_size&0xFF,out);

    /* zstd compress with um2 dictionary (level 1 for speed) */
    long bound = ZSTD_compressBound(pp_size);
    unsigned char *zout = malloc(bound);
    if (!zout) { free(pp); return 1; }
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    long csz = ZSTD_compress_usingDict(cctx, zout, bound, pp, pp_size,
                                        TCAM2_DICT, TCAM2_DICT_SIZE, 1);
    ZSTD_freeCCtx(cctx);
    if (ZSTD_isError(csz)) { free(pp); free(zout); return 1; }
    fwrite(zout, 1, csz, out);
    free(zout); free(pp);

    fprintf(stderr, "TCAM2: %ld -> %ld bytes (%.1f%%)\n",
            pp_size, ftell(out), 100.0 * ftell(out) / (pp_size ? pp_size : 1));
    return 0;
}
