/* TCAM2 encoder — zstd compression on um2 data.
   Domain-aware preprocessing (delta scalefactors, bit alloc dedup)
   prototyped but disabled — needs parser fbpos alignment fix.
   Copyright (C) 2026 Tovy. GPLv3. */
#include "tcam2.h"
#include <zstd.h>

int tcam2_compress(FILE *in, FILE *out) {
    long sz=0,cap=65536;unsigned char*data=malloc(cap);if(!data)return 1;int c;
    while((c=getc(in))!=EOF){if(sz>=cap){cap*=2;data=realloc(data,cap);}data[sz++]=c;}
    putc('T',out);putc('C',out);putc('A',out);putc('M',out);putc('2',out);
    putc((sz>>24)&0xFF,out);putc((sz>>16)&0xFF,out);
    putc((sz>>8)&0xFF,out);putc(sz&0xFF,out);
    long bound=ZSTD_compressBound(sz);
    unsigned char*zout=malloc(bound);if(!zout){free(data);return 1;}
    long csz=ZSTD_compress(zout,bound,data,sz,1);
    if(ZSTD_isError(csz)){free(data);free(zout);return 1;}
    fwrite(zout,1,csz,out);free(zout);free(data);
    fprintf(stderr,"TCAM2: %ld -> %ld bytes (%.1f%%)\n",
            sz,ftell(out),100.0*ftell(out)/(sz?sz:1));
    return 0;
}
