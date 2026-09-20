#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "pal_eos/eos.h"

/* Fake tick source: the executive only ever reads monotonic_ticks, so this is
 * enough to drive it deterministically. */
static uint64_t g_now;

static uint64_t fake_now(void *self) {
    (void)self;
    return g_now;
}

static const edge_clock_port_t g_clock = {
    .monotonic_ticks = fake_now, .wall_time = NULL, .self = NULL};

/* Dispatch trace: which task ran, in order. */
static uint32_t g_trace[64];
static uint32_t g_trace_len;

static void traced_task(void *arg) {
    if (g_trace_len < (uint32_t)(sizeof(g_trace) / sizeof(g_trace[0])))
        g_trace[g_trace_len++] = (uint32_t)(uintptr_t)arg;
}

/* A task that stops the executive, so the never-returning run() can be tested. */
static edge_eos_t *g_stop_target;
static uint32_t g_stop_after;

static void stopping_task(void *arg) {
    (void)arg;
    ++g_trace_len;
    if (g_stop_target != NULL && g_trace_len >= g_stop_after)
        edge_eos_stop(g_stop_target);
}

static edge_eos_t make_eos(void) {
    edge_eos_t eos;
    g_now = 0u;
    g_trace_len = 0u;
    assert_int_equal(edge_eos_init(&eos, &g_clock), EDGE_OK);
    return eos;
}

static void test_registration_limits(void **state) {
    (void)state;
    edge_eos_t eos = make_eos();

    assert_int_equal(edge_eos_task_add(NULL, "x", traced_task, NULL, 0u, 0u), EDGE_EINVAL);
    assert_int_equal(edge_eos_task_add(&eos, "x", NULL, NULL, 0u, 0u), EDGE_EINVAL);
    /* Priority is 0..MAX-1; out of range is refused rather than clamped. */
    assert_int_equal(edge_eos_task_add(&eos, "x", traced_task, NULL, EDGE_EOS_MAX_PRIORITIES, 0u),
                     EDGE_EINVAL);
    for (uint32_t i = 0u; i < EDGE_EOS_MAX_TASKS; ++i)
        assert_int_equal(edge_eos_task_add(&eos, "t", traced_task, NULL, 0u, 0u), EDGE_OK);
    assert_int_equal(edge_eos_task_add(&eos, "overflow", traced_task, NULL, 0u, 0u), EDGE_ENOSPC);
    assert_int_equal(eos.count, EDGE_EOS_MAX_TASKS);
}

/* Priority order within one round, and one dispatch per due task. */
static void test_priority_order_within_a_round(void **state) {
    (void)state;
    edge_eos_t eos = make_eos();

    /* Registered high-priority-first on purpose: the order must come from the
     * priority field, not from registration order, or this test proves nothing. */
    assert_int_equal(edge_eos_task_add(&eos, "high", traced_task, (void *)1, 0u, 0u), EDGE_OK);
    assert_int_equal(edge_eos_task_add(&eos, "mid", traced_task, (void *)2, 1u, 0u), EDGE_OK);
    assert_int_equal(edge_eos_task_add(&eos, "low", traced_task, (void *)3, 2u, 0u), EDGE_OK);

    assert_int_equal(edge_eos_run_once(&eos), 3u);
    assert_int_equal(g_trace_len, 3u);
    assert_int_equal(g_trace[0], 1u);
    assert_int_equal(g_trace[1], 2u);
    assert_int_equal(g_trace[2], 3u);
    assert_int_equal(eos.dispatches, 3u);
    assert_int_equal(eos.idle_rounds, 0u);
}

/*
 * The anti-starvation property is a design consequence of "one round per due
 * task", and it is why this executive is usable at all: a strict-priority
 * run-to-completion scheduler would let the highest priority task starve the rest.
 */
static void test_lower_priority_is_not_starved(void **state) {
    (void)state;
    edge_eos_t eos = make_eos();

    assert_int_equal(edge_eos_task_add(&eos, "high", traced_task, (void *)1, 0u, 1u), EDGE_OK);
    assert_int_equal(edge_eos_task_add(&eos, "low", traced_task, (void *)2, 3u, 1u), EDGE_OK);

    for (uint32_t tick = 0u; tick < 100u; ++tick) {
        g_now = tick;
        (void)edge_eos_run_once(&eos);
    }
    /* Both were due every round after the first, so both ran every round: the
     * high-priority task never consumed the lower one's turn. */
    assert_true(eos.tasks[0].runs >= 99u);
    assert_true(eos.tasks[1].runs >= 99u);
    assert_int_equal(eos.tasks[0].runs, eos.tasks[1].runs);
}

/* Fixed rate, no catch-up: a task late by many periods runs once, so the work in
 * a round stays bounded by the task count. */
static void test_periodic_deadlines_do_not_accumulate(void **state) {
    (void)state;
    edge_eos_t eos = make_eos();

    assert_int_equal(edge_eos_task_add(&eos, "p3", traced_task, (void *)1, 0u, 3u), EDGE_OK);

    g_now = 0u;
    assert_int_equal(edge_eos_run_once(&eos), 0u); /* not due in the registering round */
    g_now = 1u;
    assert_int_equal(edge_eos_run_once(&eos), 0u);
    g_now = 2u;
    assert_int_equal(edge_eos_run_once(&eos), 0u);
    g_now = 3u;
    assert_int_equal(edge_eos_run_once(&eos), 1u);
    /* A long stall (100 ticks) must produce one run, not 33. */
    g_now = 103u;
    assert_int_equal(edge_eos_run_once(&eos), 1u);
    assert_int_equal(eos.tasks[0].runs, 2u);
    assert_int_equal(eos.rounds, 5u);
}

static void test_idle_rounds_are_counted(void **state) {
    (void)state;
    edge_eos_t eos = make_eos();
    assert_int_equal(edge_eos_task_add(&eos, "p10", traced_task, (void *)1, 0u, 10u), EDGE_OK);

    g_now = 0u;
    assert_int_equal(edge_eos_run_once(&eos), 0u);
    assert_int_equal(eos.idle_rounds, 1u);
    g_now = 10u;
    assert_int_equal(edge_eos_run_once(&eos), 1u);
    assert_int_equal(eos.idle_rounds, 1u);
}

static void test_run_until_stop(void **state) {
    (void)state;
    edge_eos_t eos = make_eos();
    g_stop_target = &eos;
    g_stop_after = 3u;
    g_trace_len = 0u;
    assert_int_equal(edge_eos_task_add(&eos, "stop", stopping_task, NULL, 0u, 0u), EDGE_OK);

    assert_true(edge_eos_running(&eos) == false);
    edge_eos_run(&eos); /* returns only because the task stopped it */
    assert_false(edge_eos_running(&eos));
    assert_true(g_trace_len >= 3u);

    g_stop_target = NULL;
    edge_eos_stop(NULL); /* no state is not a crash */
    assert_false(edge_eos_running(NULL));
    assert_int_equal(edge_eos_run_once(NULL), 0u);
    edge_eos_run(NULL);
}

static void test_null_clock_is_a_fixed_zero(void **state) {
    (void)state;
    edge_eos_t eos;
    assert_int_equal(edge_eos_init(&eos, NULL), EDGE_OK);
    assert_int_equal(edge_eos_task_add(&eos, "t", traced_task, NULL, 0u, 4u), EDGE_OK);
    /* Without a clock, now is 0 and next_due is 4, so it is never due. */
    assert_int_equal(edge_eos_run_once(&eos), 0u);
    assert_int_equal(eos.idle_rounds, 1u);
    assert_int_equal(edge_eos_init(NULL, NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_registration_limits),
        cmocka_unit_test(test_priority_order_within_a_round),
        cmocka_unit_test(test_lower_priority_is_not_starved),
        cmocka_unit_test(test_periodic_deadlines_do_not_accumulate),
        cmocka_unit_test(test_idle_rounds_are_counted),
        cmocka_unit_test(test_run_until_stop),
        cmocka_unit_test(test_null_clock_is_a_fixed_zero),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
