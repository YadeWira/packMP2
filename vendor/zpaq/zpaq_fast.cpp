/* zpaq-fast: stripped-down zpaq for single-stream compression/decompression.
   stdin -> (de)compress -> stdout. No journaling/archive overhead.
   Usage: zpaq-fast [level|d]   level=1..5 (default 5), d=decompress */
#include "libzpaq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace libzpaq {
    void error(const char* msg) {
        fprintf(stderr, "zpaq-fast: %s\n", msg);
        exit(1);
    }
}

int main(int argc, char **argv) {
    int decompress = 0, level = 5;
    if (argc > 1) {
        if (strcmp(argv[1], "d") == 0 || strcmp(argv[1], "-d") == 0)
            decompress = 1;
        else { level = atoi(argv[1]); if (level < 1) level = 1; if (level > 5) level = 5; }
    }

    /* Read all stdin */
    size_t cap = 65536, size = 0;
    unsigned char *data = (unsigned char*)malloc(cap);
    if (!data) return 1;
    int c;
    while ((c = getchar()) != EOF) {
        if (size >= cap) { cap *= 2; data = (unsigned char*)realloc(data, cap); }
        data[size++] = (unsigned char)c;
    }

    if (decompress) {
        libzpaq::StringBuffer in, out;
        in.write((const char*)data, size);
        libzpaq::Decompresser d;
        d.setInput(&in);
        d.setOutput(&out);
        if (!d.findBlock()) { fprintf(stderr,"zpaq-fast: not a valid zpaq block\n"); free(data); return 1; }
        while (d.findFilename()) { d.readComment(); d.decompress(); d.readSegmentEnd(); }
        fwrite(out.data(), 1, out.size(), stdout);
    } else {
        char method[8];
        snprintf(method, sizeof(method), "%d", level);
        libzpaq::StringBuffer in, out;
        in.write((const char*)data, size);
        libzpaq::compressBlock(&in, &out, method, "", "", false);
        fwrite(out.data(), 1, out.size(), stdout);
    }
    free(data);
    return 0;
}
