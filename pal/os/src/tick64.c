#include "pal_os/tick64.h"

#include <stddef.h>

void edge_tick64_reset(edge_tick64_t *state) {
    if (state == NULL)
        return;
    state->last = 0u;
    state->wraps = 0u;
}

uint64_t edge_tick64_extend(edge_tick64_t *state, uint32_t now) {
    if (state == NULL)
        return (uint64_t)now;
    /* Unsigned compare on purpose: `now` below `last` means the counter passed
     * 2^32, not that time went backwards. Valid while sampling at least once per
     * 2^31 ticks (see the header). */
    if (now < state->last)
        ++state->wraps;
    state->last = now;
    return ((uint64_t)state->wraps << 32) | (uint64_t)now;
}
