#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"

static uint64_t g_now;
static int g_enter;
static int g_exit;
static int g_depth;
static int g_max_depth;

static uint64_t fake_clock(void *self) {
    (void)self;
    return ++g_now;
}

static void guard_enter(void *self) {
    (void)self;
    ++g_enter;
    ++g_depth;
    if (g_depth > g_max_depth) {
        g_max_depth = g_depth;
    }
}

static void guard_exit(void *self) {
    (void)self;
    ++g_exit;
    --g_depth;
}

static void test_queue_init_validates_arguments(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;

    assert_int_equal(edge_event_queue_init(NULL, storage, 4u), EDGE_EINVAL);
    assert_int_equal(edge_event_queue_init(&queue, NULL, 4u), EDGE_EINVAL);
    assert_int_equal(edge_event_queue_init(&queue, storage, 1u), EDGE_EINVAL);
    assert_int_equal(edge_event_queue_init(&queue, storage, 2u), EDGE_OK);
    assert_int_equal(edge_event_count(&queue), 0u);
}

static void test_push_pop_fifo_and_count(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_t out;

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    for (uint32_t i = 1u; i <= 3u; ++i) {
        const edge_event_t event = {.id = i, .source = i, .arg0 = i, .arg1 = i};
        assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    }
    assert_int_equal(edge_event_count(&queue), 3u);
    for (uint32_t i = 1u; i <= 3u; ++i) {
        assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
        assert_int_equal(out.id, i);
        assert_int_equal(out.arg1, i);
    }
    assert_int_equal(edge_event_count(&queue), 0u);
}

static void test_push_validates_arguments(void **state) {
    (void)state;
    edge_event_t storage[2];
    edge_event_queue_t queue;
    edge_event_t event = {.id = 1u};

    assert_int_equal(edge_event_queue_init(&queue, storage, 2u), EDGE_OK);
    assert_int_equal(edge_event_push_isr(NULL, &event), EDGE_EINVAL);
    assert_int_equal(edge_event_push_isr(&queue, NULL), EDGE_EINVAL);

    edge_event_queue_t uninitialised = {0};
    assert_int_equal(edge_event_push_isr(&uninitialised, &event), EDGE_EINVAL);
}

static void test_pop_empty_and_invalid(void **state) {
    (void)state;
    edge_event_t storage[2];
    edge_event_queue_t queue;
    edge_event_t out;

    assert_int_equal(edge_event_queue_init(&queue, storage, 2u), EDGE_OK);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_ENOENT);
    assert_int_equal(edge_event_pop(NULL, &out), EDGE_EINVAL);
    assert_int_equal(edge_event_pop(&queue, NULL), EDGE_EINVAL);
}

static void test_bounded_drop_newest_and_counter(void **state) {
    (void)state;
    edge_event_t storage[3];
    edge_event_queue_t queue;
    const edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 3u), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_EOVERFLOW);
    assert_int_equal(edge_event_count(&queue), 2u);
    assert_int_equal(edge_event_dropped(&queue), 1u);
}

static void test_wraparound_preserves_order(void **state) {
    (void)state;
    edge_event_t storage[3];
    edge_event_queue_t queue;
    edge_event_t out;

    assert_int_equal(edge_event_queue_init(&queue, storage, 3u), EDGE_OK);
    const edge_event_t a = {.id = 0xAAu};
    const edge_event_t b = {.id = 0xBBu};
    const edge_event_t c = {.id = 0xCCu};

    assert_int_equal(edge_event_push_isr(&queue, &a), EDGE_OK);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, 0xAAu);
    assert_int_equal(edge_event_push_isr(&queue, &b), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &c), EDGE_OK);
    assert_int_equal(edge_event_count(&queue), 2u);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, 0xBBu);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, 0xCCu);
}

/*
 * #167: the test above does walk the indices past the end, but it never fills the
 * ring *after* wrapping - which is where the full-queue branch lives (drop-newest
 * plus the counter) and where an off-by-one in the modulo would lose a slot.
 */
static void test_full_after_wrap_drops_the_newest(void **state) {
    (void)state;
    edge_event_t storage[3];
    edge_event_queue_t queue;
    edge_event_t out;
    const edge_event_t a = {.id = 0xAAu};
    const edge_event_t b = {.id = 0xBBu};
    const edge_event_t c = {.id = 0xCCu};
    const edge_event_t d = {.id = 0xDDu};

    assert_int_equal(edge_event_queue_init(&queue, storage, 3u), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &a), EDGE_OK);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK); /* head 1, tail 1 */
    assert_int_equal(edge_event_push_isr(&queue, &b), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &c), EDGE_OK); /* head wraps to 0 */
    assert_int_equal(edge_event_count(&queue), 2u);

    /* Full after the wrap: the newest is refused, never written over an older one. */
    assert_int_equal(edge_event_push_isr(&queue, &d), EDGE_EOVERFLOW);
    assert_int_equal(edge_event_dropped(&queue), 1u);
    assert_int_equal(edge_event_count(&queue), 2u);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, 0xBBu);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, 0xCCu);

    /* And the slot the pops freed is usable again. */
    assert_int_equal(edge_event_push_isr(&queue, &d), EDGE_OK);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, 0xDDu);
}

static void test_sink_stamps_with_clock_and_guard(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_t out;
    edge_clock_port_t clock = {.monotonic_ticks = fake_clock, .wall_time = NULL};
    edge_irq_guard_t guard = {.enter = guard_enter, .exit = guard_exit};
    edge_event_sink_t sink;
    const edge_event_t event = {.id = EDGE_EVT_UART0_RX, .source = 5u};

    g_now = 100u;
    g_enter = 0;
    g_exit = 0;
    g_depth = 0;
    g_max_depth = 0;

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    sink = (edge_event_sink_t){.queue = &queue, .clock = &clock, .guard = &guard};

    assert_int_equal(edge_event_sink_push_isr(&sink, &event), EDGE_OK);
    assert_int_equal(g_enter, 1);
    assert_int_equal(g_exit, 1);
    assert_int_equal(g_max_depth, 1);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.timestamp, 101u);
    assert_int_equal(out.source, 5u);
}

static void test_sink_without_clock_preserves_timestamp(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_t out;
    edge_event_sink_t sink;
    const edge_event_t event = {.id = EDGE_EVT_UART0_RX, .timestamp = 42u};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    sink = (edge_event_sink_t){.queue = &queue, .clock = NULL, .guard = NULL};
    assert_int_equal(edge_event_sink_push_isr(&sink, &event), EDGE_OK);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.timestamp, 42u);
}

static void test_sink_validates_arguments(void **state) {
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_sink_t sink;
    const edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    sink = (edge_event_sink_t){.queue = &queue, .clock = NULL, .guard = NULL};
    assert_int_equal(edge_event_sink_push_isr(NULL, &event), EDGE_EINVAL);
    assert_int_equal(edge_event_sink_push_isr(&sink, NULL), EDGE_EINVAL);

    edge_event_sink_t empty = {0};
    assert_int_equal(edge_event_sink_push_isr(&empty, &event), EDGE_EINVAL);
}

static void test_null_safe_counters(void **state) {
    (void)state;
    assert_int_equal(edge_event_count(NULL), 0u);
    assert_int_equal(edge_event_dropped(NULL), 0u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_queue_init_validates_arguments),
        cmocka_unit_test(test_push_pop_fifo_and_count),
        cmocka_unit_test(test_push_validates_arguments),
        cmocka_unit_test(test_pop_empty_and_invalid),
        cmocka_unit_test(test_bounded_drop_newest_and_counter),
        cmocka_unit_test(test_wraparound_preserves_order),
        cmocka_unit_test(test_full_after_wrap_drops_the_newest),
        cmocka_unit_test(test_sink_stamps_with_clock_and_guard),
        cmocka_unit_test(test_sink_without_clock_preserves_timestamp),
        cmocka_unit_test(test_sink_validates_arguments),
        cmocka_unit_test(test_null_safe_counters),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
