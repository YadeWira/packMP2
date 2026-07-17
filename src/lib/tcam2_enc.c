/* TCAM2 encoder — zstd with um2 dictionary.
   Copyright (C) 2026 Tovy. GPLv3. */
#include "tcam2.h"
#include <zstd.h>
#include "tcam2_dict.h"

int tcam2_quiet = 0;  /* NOTE: not thread-safe (cosmetic only, see tcam2.h) */

int tcam2_compress_dict(FILE *in, FILE *out, int level,
                         const unsigned char *dict, size_t dict_size) {
    long sz=0,cap=65536;unsigned char*data=malloc(cap);if(!data)return 1;int c;
    while((c=getc(in))!=EOF){if(sz>=cap){cap*=2;data=realloc(data,cap);}data[sz++]=c;}

    long bound=ZSTD_compressBound(sz);
    unsigned char*zout=malloc(bound);if(!zout){free(data);return 1;}
    ZSTD_CCtx*cctx=ZSTD_createCCtx();
    if(level<1)level=1;if(level>9)level=9;
    long csz;
    if(dict && dict_size>0)
        csz=ZSTD_compress_usingDict(cctx,zout,bound,data,sz,dict,dict_size,level);
    else
        csz=ZSTD_compress2(cctx,zout,bound,data,sz);
    ZSTD_freeCCtx(cctx);
    if(ZSTD_isError(csz)){free(data);free(zout);return 1;}

    /* never-expand guard: on small inputs the zstd frame + dict-ID overhead
       can exceed the input size (e.g. um2 files under ~100KB). Store
       verbatim instead so tcam2 output is never larger than the input. */
    int stored = (csz >= sz);

    putc('T',out);putc('C',out);putc('A',out);putc('M',out);putc('2',out);
    putc(stored,out);
    putc((sz>>24)&0xFF,out);putc((sz>>16)&0xFF,out);
    putc((sz>>8)&0xFF,out);putc(sz&0xFF,out);
    if (stored) fwrite(data,1,sz,out);
    else        fwrite(zout,1,csz,out);
    free(zout);free(data);
    fflush(out);
    if(!tcam2_quiet) fprintf(stderr,"TCAM2: %ld -> %ld bytes (%.1f%%)%s\n",sz,ftell(out),
        100.0*ftell(out)/(sz?sz:1), stored?" [stored]":"");
    return 0;
}

int tcam2_compress(FILE *in, FILE *out, int level) {
    return tcam2_compress_dict(in, out, level, TCAM2_DICT, TCAM2_DICT_SIZE);
}
