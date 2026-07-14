/* TCAM2 decoder — zstd decompress.
   Copyright (C) 2026 Tovy. GPLv3. */
#include "tcam2.h"
#include <zstd.h>

int tcam2_decompress(FILE *in, FILE *out) {
    if(getc(in)!='T'||getc(in)!='C'||getc(in)!='A'||getc(in)!='M'||getc(in)!='2')
    {fprintf(stderr,"TCAM2: bad header\n");return 1;}
    long osz=((long)getc(in)<<24)|((long)getc(in)<<16)|((long)getc(in)<<8)|getc(in);
    long csz=0,ccap=65536;unsigned char*cmp=malloc(ccap);if(!cmp)return 1;int c;
    while((c=getc(in))!=EOF){if(csz>=ccap){ccap*=2;cmp=realloc(cmp,ccap);}cmp[csz++]=c;}
    unsigned long long dsz=ZSTD_getFrameContentSize(cmp,csz);
    if(dsz==ZSTD_CONTENTSIZE_ERROR||dsz==ZSTD_CONTENTSIZE_UNKNOWN)dsz=osz;
    unsigned char*data=malloc(dsz);if(!data){free(cmp);return 1;}
    long act=ZSTD_decompress(data,dsz,cmp,csz);free(cmp);
    if(ZSTD_isError(act)||act!=(long)dsz){free(data);return 1;}
    fwrite(data,1,dsz,out);free(data);
    return 0;
}
