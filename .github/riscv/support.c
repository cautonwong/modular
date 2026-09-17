/* Minimal freestanding memory routines.
 *
 * RV32 newlib is not available in the CI image, so the firmware links with
 * `-nostdlib` and provides the few symbols the compiler may emit for local
 * aggregate initialisation. */
#include <stddef.h>

void *memset(void *dst, int value, size_t len) {
    unsigned char *out = (unsigned char *)dst;
    for (size_t i = 0u; i < len; ++i)
        out[i] = (unsigned char)value;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t len) {
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;
    for (size_t i = 0u; i < len; ++i)
        out[i] = in[i];
    return dst;
}

void *memmove(void *dst, const void *src, size_t len) {
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;
    if (out < in) {
        for (size_t i = 0u; i < len; ++i)
            out[i] = in[i];
    } else if (out > in) {
        for (size_t i = len; i > 0u; --i)
            out[i - 1u] = in[i - 1u];
    }
    return dst;
}
