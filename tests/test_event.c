#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"

static uint64_t fake_clock(void *self)
{
    return *(uint64_t *)self;
}

static void test_fifo_and_timestamp(void **state)
{
    (void)state;
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_sink_t sink;
    uint64_t now = 1234u;
    edge_event_t event = {.id = EDGE_EVT_UART0_RX, .source = 7u,
                          .arg0 = 8u, .arg1 = 9u, .timestamp = 0u};
    edge_event_t out;

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    sink = (edge_event_sink_t){&queue, fake_clock, &now};
    assert_int_equal(edge_event_sink_push_isr(&sink, &event), EDGE_OK);
    assert_int_equal(edge_event_pop(&queue, &out), EDGE_OK);
    assert_int_equal(out.id, event.id);
    assert_int_equal(out.source, 7u);
    assert_int_equal(out.timestamp, 1234u);
    assert_int_equal(edge_event_count(&queue), 0u);
}

static void test_bounded_drop_newest(void **state)
{
    (void)state;
    edge_event_t storage[3];
    edge_event_queue_t queue;
    edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 3u), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_EOVERFLOW);
    assert_int_equal(edge_event_count(&queue), 2u);
    assert_int_equal(edge_event_dropped(&queue), 1u);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fifo_and_timestamp),
        cmocka_unit_test(test_bounded_drop_newest),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
