#ifndef DEMO_H
#define DEMO_H

/*
 * Negative fixture for the opaque-storage rule: it promises the caller a size and
 * an alignment for a type whose fields are still visible in the same header, which
 * is exactly the shape the rule forbids - the caller is told how much memory to
 * provide and can then reach into it anyway.
 */

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define DEMO_STORAGE_SIZE 64u
#define DEMO_STORAGE_ALIGN alignof(max_align_t)

typedef struct demo {
    uint32_t counter;
    uint8_t buffer[16];
} demo_t;

void demo_construct(demo_t *self, uint32_t id);

#endif /* DEMO_H */
