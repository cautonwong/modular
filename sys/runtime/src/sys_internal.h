#ifndef EDGE_SYS_INTERNAL_H
#define EDGE_SYS_INTERNAL_H

/*
 * The system's real state, kept out of the public header (#256).
 *
 * `edge_sys_t` in `runtime/sys.h` is an opaque, aligned storage block of
 * `EDGE_SYS_STORAGE_SIZE` bytes; this is what lives in it. Two reasons the fields
 * are not public:
 *
 * - `pending_head` / `pending_tail` / `pending_count` are the ring's invariants.
 *   Anyone could previously write one of them and leave the queue in a state the
 *   type cannot represent, with no place for `sys` to refuse.
 * - `subscriptions` / `subscription_count` could be appended to directly, which
 *   silently defeats the required-subscription rule.
 *
 * The composition root still owns the memory (a plain `edge_sys_t` on the stack or
 * in `.bss`); it just cannot reach inside it.
 */
#include "runtime/sys.h"

struct edge_sys_internal {
    edge_module_t **apps;
    size_t app_count;
    edge_sys_subscription_t *subscriptions;
    size_t subscription_count;
    size_t subscription_capacity;
    const uint32_t *required_ids;
    size_t required_count;
    edge_event_queue_t *events;
    const edge_clock_port_t *clock;
    uint32_t max_events_per_run;
    uint64_t tick;
    edge_sys_stats_t stats;
    edge_sys_state_t state;
    edge_event_t *pending;
    uint32_t pending_capacity;
    uint32_t pending_head;
    uint32_t pending_tail;
    uint32_t pending_count;
    edge_sys_idle_fn idle_hook;
    void *idle_ctx;
};

/* Exact, not `<=`: the margin above is a measured number, so a field added to the
 * struct fails here and says to re-measure both constants, instead of silently
 * eating the slack until the day the caller under-allocates. */
_Static_assert(EDGE_SYS_STORAGE_SIZE - sizeof(struct edge_sys_internal) == EDGE_SYS_STORAGE_MARGIN,
               "edge_sys_t storage drifted: re-measure EDGE_SYS_STORAGE_SIZE and _MARGIN");
_Static_assert(EDGE_SYS_STORAGE_ALIGN >= _Alignof(struct edge_sys_internal),
               "EDGE_SYS_STORAGE_ALIGN is stale: the storage would be under-aligned");

/* The public handle is caller-owned bytes; these are how the implementation reaches
 * the state inside them.
 *
 * Two accessors, because the const qualifier on a parameter is a promise to the
 * caller and it is worth keeping: `edge_sys_state_const()` is what a read-only
 * entry point uses, `edge_sys_state()` is for the ones that write. Both cast a byte
 * block to a struct pointer - the practice every caller-alloc contract in this tree
 * relies on - so type safety here comes from the assertions below, not from the
 * cast. The mutable accessor accepts a const handle because the C type system cannot
 * express "these bytes are mine": treat a `const edge_sys_t *` reaching the mutable
 * accessor as a bug in the caller, not as permission. */
static inline struct edge_sys_internal *edge_sys_state(const edge_sys_t *handle) {
    return (struct edge_sys_internal *)(void *)(uintptr_t)handle;
}

static inline const struct edge_sys_internal *edge_sys_state_const(const edge_sys_t *handle) {
    return (const struct edge_sys_internal *)(const void *)(uintptr_t)handle;
}

#endif /* EDGE_SYS_INTERNAL_H */
