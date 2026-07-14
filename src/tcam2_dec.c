/*  TCAM2 decoder — zlib inflate.
    Copyright (C) 2026 Tovy. GPLv3.
*/
#include "tcam2.h"
#include <zlib.h>

int tcam2_decompress(FILE *in, FILE *out) {
    if(getc(in)!='T'||getc(in)!='C'||getc(in)!='A'||getc(in)!='M'||getc(in)!='2')
    {fprintf(stderr,"TCAM2: bad header\n");return 1;}
    long orig=((long)getc(in)<<24)|((long)getc(in)<<16)|
              ((long)getc(in)<<8)|((long)getc(in));
    long csz=0,ccap=65536;
    unsigned char*cmp=(unsigned char*)malloc(ccap);if(!cmp)return 1;
    int c;while((c=getc(in))!=EOF){
        if(csz>=ccap){ccap*=2;cmp=(unsigned char*)realloc(cmp,ccap);}
        cmp[csz++]=(unsigned char)c;}
    unsigned char*data=(unsigned char*)malloc(orig);if(!data){free(cmp);return 1;}
    z_stream z;z.zalloc=Z_NULL;z.zfree=Z_NULL;z.opaque=Z_NULL;
    z.next_in=cmp;z.avail_in=csz;z.next_out=data;z.avail_out=orig;
    inflateInit2(&z,-15);
    if(inflate(&z,Z_FINISH)!=Z_STREAM_END){free(cmp);free(data);return 1;}
    inflateEnd(&z);free(cmp);
    fwrite(data,1,orig,out);
    free(data);
    return 0;
}
