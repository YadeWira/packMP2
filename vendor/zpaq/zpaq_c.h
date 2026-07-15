/* zpaq C interface — extern "C" wrappers for use from C code.
   Link with g++ (C++ linker required for libzpaq). */
#ifndef ZPAQ_C_H
#define ZPAQ_C_H
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compress data[in_size] into *out (malloc'd, caller frees).
   level: 1..5 (5 = best ratio). Returns 0 on success. */
int zpaq_compress(const unsigned char *in, size_t in_size,
                  unsigned char **out, size_t *out_size, int level);

/* Compress with raw ZPAQL method string (e.g. "x6,0ci1,1,1,1,2am").
   For experimenting with custom methods. Returns 0 on success. */
int zpaq_compress_method(const unsigned char *in, size_t in_size,
                         unsigned char **out, size_t *out_size,
                         const char *method);

/* Decompress data[in_size] into *out (malloc'd, caller frees).
   Returns 0 on success. */
int zpaq_decompress(const unsigned char *in, size_t in_size,
                    unsigned char **out, size_t *out_size);

#ifdef __cplusplus
}
#endif
#endif
