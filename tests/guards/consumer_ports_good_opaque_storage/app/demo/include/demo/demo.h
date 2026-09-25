#ifndef DEMO_H
#define DEMO_H

/*
 * Positive fixture for the opaque-storage rule: the public header tells the caller
 * how much memory to provide and how to align it, and nothing else. The fields live
 * in src/demo.c with the assertions that keep this contract honest.
 */

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define DEMO_STORAGE_SIZE 64u
#define DEMO_STORAGE_ALIGN alignof(max_align_t)

typedef struct demo demo_t;

void demo_construct(demo_t *self, uint32_t id);
uint32_t demo_counter(const demo_t *self);

#endif /* DEMO_H */
