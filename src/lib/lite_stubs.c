/* lite_stubs.c — stub implementations for TCAM2/zpaq functions.
   Linked only in lite build (unpack/pack only, no compression). */
#include <stdio.h>
#include <stddef.h>

/* TCAM2 stubs */
int tcam2_compress(FILE *in, FILE *out, int level) {
    (void)in; (void)out; (void)level;
    fprintf(stderr, "packMP2: TCAM2 not available in lite build\n");
    return 1;
}
int tcam2_compress_dict(FILE *in, FILE *out, int level,
                         const unsigned char *dict, size_t dict_size) {
    (void)in; (void)out; (void)level; (void)dict; (void)dict_size;
    fprintf(stderr, "packMP2: TCAM2 not available in lite build\n");
    return 1;
}
int tcam2_decompress(FILE *in, FILE *out) {
    (void)in; (void)out;
    fprintf(stderr, "packMP2: TCAM2 not available in lite build\n");
    return 1;
}
int tcam2_decompress_dict(FILE *in, FILE *out,
                           const unsigned char *dict, size_t dict_size) {
    (void)in; (void)out; (void)dict; (void)dict_size;
    fprintf(stderr, "packMP2: TCAM2 not available in lite build\n");
    return 1;
}
int tcam2_quiet = 0;

/* zpaq stubs */
int zpaq_compress(const unsigned char *in, size_t in_size,
                  unsigned char **out, size_t *out_size, int level) {
    (void)in; (void)in_size; (void)out; (void)out_size; (void)level;
    fprintf(stderr, "packMP2: zpaq not available in lite build\n");
    return 1;
}
int zpaq_decompress(const unsigned char *in, size_t in_size,
                    unsigned char **out, size_t *out_size) {
    (void)in; (void)in_size; (void)out; (void)out_size;
    fprintf(stderr, "packMP2: zpaq not available in lite build\n");
    return 1;
}
