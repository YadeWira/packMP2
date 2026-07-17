/* TCAM2 decoder — zstd decompress with um2 dictionary.
   Copyright (C) 2026 Tovy. GPLv3. */
#include "tcam2.h"
#include <zstd.h>
#include "tcam2_dict.h"

int tcam2_decompress_dict(FILE *in, FILE *out,
                           const unsigned char *dict, size_t dict_size) {
    if(getc(in)!='T'||getc(in)!='C'||getc(in)!='A'||getc(in)!='M'||getc(in)!='2')
    {fprintf(stderr,"TCAM2: bad header\n");return 1;}
    int stored=getc(in);
    long osz=((long)getc(in)<<24)|((long)getc(in)<<16)|((long)getc(in)<<8)|getc(in);
    long csz=0,ccap=65536;unsigned char*cmp=malloc(ccap);if(!cmp)return 1;int c;
    while((c=getc(in))!=EOF){if(csz>=ccap){ccap*=2;cmp=realloc(cmp,ccap);}cmp[csz++]=c;}

    if (stored) {
        /* verbatim passthrough (never-expand guard chose stored on encode) */
        if (csz != osz) { free(cmp); fprintf(stderr,"TCAM2: corrupt stored block\n"); return 1; }
        fwrite(cmp,1,csz,out); fflush(out); free(cmp);
        return 0;
    }

    unsigned long long dsz=ZSTD_getFrameContentSize(cmp,csz);
    if(dsz==ZSTD_CONTENTSIZE_ERROR||dsz==ZSTD_CONTENTSIZE_UNKNOWN)dsz=osz;
    unsigned char*data=malloc(dsz);if(!data){free(cmp);return 1;}
    ZSTD_DCtx*dctx=ZSTD_createDCtx();
    long act;
    if(dict && dict_size>0)
        act=ZSTD_decompress_usingDict(dctx,data,dsz,cmp,csz,dict,dict_size);
    else
        act=ZSTD_decompressDCtx(dctx,data,dsz,cmp,csz);
    ZSTD_freeDCtx(dctx);free(cmp);
    if(ZSTD_isError(act)||act!=(long)dsz){free(data);return 1;}
    fwrite(data,1,dsz,out);fflush(out);free(data);
    return 0;
}

int tcam2_decompress(FILE *in, FILE *out) {
    return tcam2_decompress_dict(in, out, TCAM2_DICT, TCAM2_DICT_SIZE);
}
