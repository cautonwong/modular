#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "edge/board_irq.h"

static int sink(const edge_event_t *event, void *arg) {
    edge_event_t *out = (edge_event_t *)arg;
    *out = *event;
    return 0;
}

static void test_forward(void **state) {
    (void)state;
    edge_event_t out = {0};
    edge_irq_route_t route = { .irq=5u, .event_id=0x0101u, .source_id=7u, .sink=sink, .sink_arg=&out };
    assert_int_equal(edge_board_irq_forward(&route, 11u, 22u), 0);
    assert_int_equal(out.id, 0x0101u);
    assert_int_equal(out.source, 7u);
    assert_int_equal(out.arg, 11u);
    assert_int_equal(out.data, 22u);
}

int main(void) {
    const struct CMUnitTest tests[] = { cmocka_unit_test(test_forward) };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
