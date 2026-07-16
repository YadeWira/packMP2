/* zpaq C interface — extern "C" wrappers for compress/decompress.
   Used by TCAM2 as alternative backend. */
#include "libzpaq.h"
#include "zpaq_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace libzpaq {
    void error(const char* msg) {
        fprintf(stderr, "zpaq: %s\n", msg);
        /* Don't exit — return error to caller. We throw but catch immediately. */
        throw msg;
    }
}

extern "C" {

int zpaq_compress(const unsigned char *in, size_t in_size,
                  unsigned char **out, size_t *out_size, int level) {
    if (level < 1) level = 1; if (level > 5) level = 5;
    /* Custom-tuned methods for um2 data (benchmarked on 19-file corpus, Jul 2026).
       Key insight: c256 context + sparse models + multi-mixer beat built-in CM
       at lower cost. MATCH helps but is expensive; SSE adds ~2KB savings at +350ms.
       Level 1: built-in LZ77 fast (store if barely compressible).
       Level 2: built-in LZ77 with longer search.
       Level 3: BWT+1ISSE+mix (574k, 323ms) — unbeatable speed, 83.1% ratio.
       Level 4: BWT+c256+2ISSE+mix (566k, 462ms) — +27ms for +0.6% vs old lvl4.
       Level 5: BWT+c256+2ISSE+MATCH+sparse+mm16+SSE (562k, 2365ms) — matches
                lpaq8 at 17% faster than built-in CM. */
    static const char *methods[] = {
        "0",              /* 0: store (unused) */
        "1",              /* 1: fast LZ77 */
        "2",              /* 2: LZ77 longer search */
        "x6,0ci1m",       /* 3: BWT+1ISSE+mix — fast + good ratio */
        "x6,0c256ci1,1m", /* 4: BWT+c256+2ISSE+mix — best speed/ratio */
        /* 5: BWT+c256+2ISSE+MATCH+sparse+mm16+SSE — matches lpaq8 */
        "x6,0c256ci1,1,2ac0,2,0,255i1c0,3,0,0,255i1c0,4,0,0,0,255i1mm16ts19t0",
    };
    return zpaq_compress_method(in, in_size, out, out_size, methods[level]);
}

int zpaq_compress_method(const unsigned char *in, size_t in_size,
                         unsigned char **out, size_t *out_size,
                         const char *method) {
    libzpaq::StringBuffer sb_in, sb_out;
    sb_in.write((const char*)in, in_size);

    try {
        libzpaq::compressBlock(&sb_in, &sb_out, method, "", "", false);
    } catch (const char*) {
        return 1;
    }

    *out_size = sb_out.size();
    *out = (unsigned char*)malloc(*out_size);
    if (!*out) return 1;
    memcpy(*out, sb_out.data(), *out_size);
    return 0;
}

int zpaq_decompress(const unsigned char *in, size_t in_size,
                    unsigned char **out, size_t *out_size) {
    libzpaq::StringBuffer sb_in, sb_out;
    sb_in.write((const char*)in, in_size);

    try {
        libzpaq::Decompresser d;
        d.setInput(&sb_in);
        d.setOutput(&sb_out);
        if (!d.findBlock()) return 1;
        while (d.findFilename()) {
            d.readComment();
            d.decompress();
            d.readSegmentEnd();
        }
    } catch (const char*) {
        return 1;
    }

    *out_size = sb_out.size();
    *out = (unsigned char*)malloc(*out_size);
    if (!*out) return 1;
    memcpy(*out, sb_out.data(), *out_size);
    return 0;
}

}  // extern "C"
