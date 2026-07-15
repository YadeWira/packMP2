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
    /* Custom-tuned methods for um2 data — faster than built-in expansion.
       Levels 1-2: built-in LZ77 (fast, good for barely-compressible data).
       Level 3: BWT+mix (574k, 0.41s) — beats old built-in 3 in ratio+speed.
       Level 4: BWT+1ISSE+mix (570k, 0.46s) — near old level 4 ratio at 2x speed.
       Level 5: built-in full CM (562k, 2.9s) — best ratio, matches lpaq8. */
    static const char *methods[] = {
        "0",              /* 0: store (unused) */
        "1",              /* 1: fast LZ77 */
        "2",              /* 2: LZ77 longer search */
        "x6,0ci1m",       /* 3: BWT+mix — fast + good ratio */
        "x6,0ci1,1m",     /* 4: BWT+1ISSE+mix — sweet spot */
        "5",              /* 5: built-in full CM — best ratio */
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
