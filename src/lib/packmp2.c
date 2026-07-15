/* packmp2.c — public C API implementation for MP2 lossless compression.
   Memory-to-memory. Uses existing unpack/pack + TCAM2/zpaq engines.
   Copyright (C) 2026 packMP2 contributors. GPLv3. */
#include "packmp2.h"
#include "unpackmp2.h"
#include "tcam2.h"
#include "zpaq_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- helpers ---- */

static int validate_opts(const packmp2_opts *opts, char msg[256]) {
    if (!opts) { snprintf(msg,256,"packmp2: opts is NULL"); return 1; }
    if (opts->method < 0 || opts->method > 2)
      { snprintf(msg,256,"packmp2: invalid method %d",opts->method); return 1; }
    if (opts->level < 0 || opts->level > 5)
      { snprintf(msg,256,"packmp2: invalid level %d",opts->level); return 1; }
    if (opts->never_expand < 0 || opts->never_expand > 1)
      { snprintf(msg,256,"packmp2: invalid never_expand %d",opts->never_expand); return 1; }
    for (int i=0; i<8; i++) {
        if (opts->reserved[i] != 0)
          { snprintf(msg,256,"packmp2: reserved[%d] must be zero (got %d)",i,opts->reserved[i]); return 1; }
    }
    msg[0]=0;
    return 0;
}

/* Read entire FILE* into malloc'd buffer. *len receives size. */
static unsigned char *slurp_file(FILE *f, size_t *len) {
    long cap=65536, sz=0; unsigned char *d=malloc(cap); if(!d) return NULL;
    int c;
    while((c=getc(f))!=EOF){if(sz>=cap){cap*=2;d=realloc(d,cap);}d[sz++]=c;}
    *len=sz;
    return d;
}

/* ---- public API ---- */

int packmp2_compress(const unsigned char *in,  size_t  in_len,
                           unsigned char **out, size_t *out_len,
                     const packmp2_opts *opts, char msg[256]) {
    if (!in || !out || !out_len) { snprintf(msg,256,"packmp2: NULL argument"); return 1; }
    *out=NULL; *out_len=0;
    msg[0]=0;

    if (validate_opts(opts, msg)) return 1;

    int method = opts->method;
    int level  = opts->level;

    /* Auto-select method based on file size */
    if (method == PACKMP2_METHOD_AUTO) {
        method = (in_len < 128*1024) ? PACKMP2_METHOD_ZSTD
                                     : PACKMP2_METHOD_ZPAQ;
    }

    if (level == PACKMP2_LEVEL_STORE) {
        *out_len = in_len;
        *out = malloc(in_len);
        if (!*out) { snprintf(msg,256,"packmp2: malloc failed"); return 1; }
        memcpy(*out, in, in_len);
        return 0;
    }

    /* Step 1: unpack mp2 -> um2 (via tmpfiles) */
    FILE *mp2_f = tmpfile(), *um2_f = tmpfile();
    if (!mp2_f || !um2_f) {
        if(mp2_f)fclose(mp2_f); if(um2_f)fclose(um2_f);
        snprintf(msg,256,"packmp2: tmpfile failed"); return 1;
    }
    fwrite(in, 1, in_len, mp2_f); rewind(mp2_f);

    int rc = unpack(mp2_f, um2_f);
    fclose(mp2_f);
    if (rc) { fclose(um2_f); snprintf(msg,256,"packmp2: unpack failed"); return 1; }

    /* Read um2 into buffer */
    rewind(um2_f);
    size_t um2_len=0;
    unsigned char *um2_data = slurp_file(um2_f, &um2_len);
    fclose(um2_f);
    if (!um2_data) { snprintf(msg,256,"packmp2: malloc failed"); return 1; }

    /* Step 2: compress um2 -> tcam2/zpaq */
    if (method == PACKMP2_METHOD_ZPAQ) {
#ifndef PACKMP2_SLIM
        int zl = level; /* 1..5 maps directly to our tuned zpaq methods */
        rc = zpaq_compress(um2_data, um2_len, out, out_len, zl);
        free(um2_data);
        if (rc) { snprintf(msg,256,"packmp2: zpaq compress failed"); return 1; }
#else
        free(um2_data);
        snprintf(msg,256,"packmp2: zpaq not available (slim build)"); return 1;
#endif
    } else {
        /* zstd: use tmpfile for tcam2_compress output */
        FILE *tc_f = tmpfile();
        if (!tc_f) { free(um2_data); snprintf(msg,256,"packmp2: tmpfile failed"); return 1; }
        FILE *um2_in = tmpfile();
        if (!um2_in) { fclose(tc_f); free(um2_data);
                        snprintf(msg,256,"packmp2: tmpfile failed"); return 1; }
        fwrite(um2_data, 1, um2_len, um2_in); rewind(um2_in);
        free(um2_data);

        /* Map library level to zstd level (clamped 1..6 for zstd) */
        int zstd_lvl = (level == 5) ? 6 : (level <= 1 ? 1 : (level == 2 ? 1 : (level <= 3 ? 1 : 3)));
        rc = tcam2_compress(um2_in, tc_f, zstd_lvl);
        fclose(um2_in);
        if (rc) { fclose(tc_f); snprintf(msg,256,"packmp2: tcam2 compress failed"); return 1; }

        /* Read compressed result */
        rewind(tc_f);
        *out = slurp_file(tc_f, out_len);
        fclose(tc_f);
        if (!*out) { snprintf(msg,256,"packmp2: malloc failed"); return 1; }
    }

    /* Step 3: never-expand guard — if compressed >= original, store verbatim */
    if (opts->never_expand && *out_len >= in_len) {
        free(*out);
        *out_len = in_len;
        *out = malloc(in_len);
        if (!*out) { snprintf(msg,256,"packmp2: malloc failed"); return 1; }
        memcpy(*out, in, in_len);
    }

    return 0;
}

int packmp2_decompress(const unsigned char *in,  size_t  in_len,
                             unsigned char **out, size_t *out_len,
                       char msg[256]) {
    if (!in || !out || !out_len) { snprintf(msg,256,"packmp2: NULL argument"); return 1; }
    *out=NULL; *out_len=0;
    msg[0]=0;

    if (in_len < 4) { snprintf(msg,256,"packmp2: input too short"); return 1; }

    /* Auto-detect format: TCAM2 = "TCAM2", zpaq = 0x37 '7', RAW2 = "RAW2" */
    unsigned char *dec_data = NULL;
    size_t dec_len = 0;

    if (memcmp(in, "TCAM", 4) == 0) {
        /* TCAM2 (zstd) */
        FILE *tc_f = tmpfile(), *um2_f = tmpfile();
        if (!tc_f || !um2_f) {
            if(tc_f)fclose(tc_f); if(um2_f)fclose(um2_f);
            snprintf(msg,256,"packmp2: tmpfile failed"); return 1;
        }
        fwrite(in, 1, in_len, tc_f); rewind(tc_f);
        int rc = tcam2_decompress(tc_f, um2_f);
        fclose(tc_f);
        if (rc) { fclose(um2_f); snprintf(msg,256,"packmp2: tcam2 decompress failed"); return 1; }
        rewind(um2_f);
        dec_data = slurp_file(um2_f, &dec_len);
        fclose(um2_f);
        if (!dec_data) { snprintf(msg,256,"packmp2: malloc failed"); return 1; }
    } else if (in[0] == 0x37) {
        /* zpaq ("7kSt" magic) */
#ifndef PACKMP2_SLIM
        int rc = zpaq_decompress(in, in_len, &dec_data, &dec_len);
        if (rc) { snprintf(msg,256,"packmp2: zpaq decompress failed"); return 1; }
#else
        snprintf(msg,256,"packmp2: zpaq not available (slim build)"); return 1;
#endif
    } else {
        snprintf(msg,256,"packmp2: unknown format (%02x%02x%02x%02x)",
                 in[0],in[1],in[2],in[3]); return 1;
    }

    /* Step 2: pack um2 -> mp2 */
    FILE *um2_f = tmpfile(), *mp2_f = tmpfile();
    if (!um2_f || !mp2_f) {
        if(um2_f)fclose(um2_f); if(mp2_f)fclose(mp2_f);
        free(dec_data); snprintf(msg,256,"packmp2: tmpfile failed"); return 1;
    }
    fwrite(dec_data, 1, dec_len, um2_f); rewind(um2_f);
    free(dec_data);
    int rc = pack(um2_f, mp2_f);
    fclose(um2_f);
    if (rc) { fclose(mp2_f); snprintf(msg,256,"packmp2: pack failed"); return 1; }

    rewind(mp2_f);
    *out = slurp_file(mp2_f, out_len);
    fclose(mp2_f);
    if (!*out) { snprintf(msg,256,"packmp2: malloc failed"); return 1; }

    return 0;
}

size_t packmp2_query_original_size(const unsigned char *in, size_t in_len,
                                   char msg[256]) {
    if (!in || in_len < 9) {
        if (msg) snprintf(msg,256,"packmp2: input too short for header");
        return 0;
    }
    if (msg) msg[0]=0;

    if (memcmp(in, "TCAM", 4) == 0 && in_len >= 10) {
        int stored = in[5];
        size_t osz = ((size_t)in[6]<<24)|((size_t)in[7]<<16)|((size_t)in[8]<<8)|in[9];
        return osz;
    }
    /* zpaq format: need to decompress to know original size */
    if (msg) snprintf(msg,256,"packmp2: cannot query zpaq size from header alone");
    return 0;
}

const char *packmp2_version(void) {
    return "0.5.0";
}
