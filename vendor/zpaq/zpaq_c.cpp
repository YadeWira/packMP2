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
    char method[8];
    snprintf(method, sizeof(method), "%d", level);

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
