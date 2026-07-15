/* zpaq-fast: stripped-down zpaq for single-stream compression. */
#include "libzpaq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Provide required error callback */
namespace libzpaq {
    void error(const char* msg) {
        fprintf(stderr, "zpaq-fast: %s\n", msg);
        exit(1);
    }
}

int main(int argc, char **argv) {
    int level = 5;
    if (argc > 1) level = atoi(argv[1]);
    if (level < 1) level = 1;
    if (level > 5) level = 5;
    char method[8];
    snprintf(method, sizeof(method), "%d", level);

    size_t cap = 65536, size = 0;
    unsigned char *data = (unsigned char*)malloc(cap);
    if (!data) return 1;
    int c;
    while ((c = getchar()) != EOF) {
        if (size >= cap) { cap *= 2; data = (unsigned char*)realloc(data, cap); }
        data[size++] = (unsigned char)c;
    }

    libzpaq::StringBuffer in, out;
    in.write((const char*)data, size);
    libzpaq::compressBlock(&in, &out, method, "", "", false);

    fwrite(out.data(), 1, out.size(), stdout);
    free(data);
    return 0;
}
