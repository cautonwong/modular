#include "demo/demo.h"

#include <assert.h>
#include <stdalign.h>

/*
 * The definition lives here, where the caller cannot see it, together with the
 * assertions that keep DEMO_STORAGE_SIZE / DEMO_STORAGE_ALIGN from going stale.
 */
struct demo {
    uint32_t counter;
    uint8_t buffer[16];
};

static_assert(sizeof(struct demo) <= DEMO_STORAGE_SIZE,
              "DEMO_STORAGE_SIZE is stale: the caller would under-allocate");
static_assert(alignof(struct demo) <= DEMO_STORAGE_ALIGN,
              "DEMO_STORAGE_ALIGN is stale: the caller would under-align");

void demo_construct(demo_t *self, uint32_t id) {
    self->counter = id;
}

uint32_t demo_counter(const demo_t *self) {
    return self->counter;
}
