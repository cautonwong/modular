#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/pal.h"
#include "pal_host/host.h"

static void test_pal_controls(void **state) {
    (void)state;
    edge_pal_port_t pal = pal_host_port();

    assert_non_null(pal.critical_enter);
    assert_non_null(pal.critical_exit);
    assert_non_null(pal.memory_barrier);
    assert_non_null(pal.monotonic_ticks);
    assert_non_null(pal.in_isr);
    assert_non_null(pal.isr_enter);
    assert_non_null(pal.isr_exit);

    assert_int_equal(pal_host_critical_depth(), 0u);
    pal.critical_enter(pal.self);
    pal.critical_enter(pal.self);
    assert_int_equal(pal_host_critical_depth(), 2u);
    pal.critical_exit(pal.self);
    pal.critical_exit(pal.self);
    pal.critical_exit(pal.self); /* unbalanced exit is clamped */
    assert_int_equal(pal_host_critical_depth(), 0u);

    pal_host_set_ticks(10u);
    assert_int_equal(pal.monotonic_ticks(pal.self), 11u);
    assert_int_equal(pal.monotonic_ticks(pal.self), 12u);

    assert_false(pal.in_isr(pal.self));
    pal.isr_enter(pal.self);
    assert_true(pal.in_isr(pal.self));
    pal.isr_exit(pal.self);
    assert_false(pal.in_isr(pal.self));
    pal_host_set_in_isr(true);
    assert_true(pal.in_isr(pal.self));

    pal.memory_barrier(pal.self);
}

static void test_pal_bridges_event_sink(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_t out;
    const edge_pal_port_t pal = pal_host_port();
    const edge_clock_port_t clock = {
        .monotonic_ticks = pal.monotonic_ticks, .wall_time = NULL, .self = pal.self};
    const edge_irq_guard_t guard = {
        .enter = pal.critical_enter, .exit = pal.critical_exit, .self = pal.self};
    edge_event_sink_t sink;
    const edge_event_t event = {.id = 0x0101u};

    pal_host_set_ticks(100u);
    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    sink = (edge_event_sink_t){.queue = &queue, .clock = &clock, .guard = &guard};
    assert_int_equal(pal_host_critical_depth(), 0u);
    assert_int_equal(edge_event_sink_push_isr(&sink, &event), EDGE_OK);
    assert_int_equal(pal_host_critical_depth(), 0u);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.timestamp, 101u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pal_controls),
        cmocka_unit_test(test_pal_bridges_event_sink),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
