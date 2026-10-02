#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmocka.h>

#include "ble_nav/ble_nav.h"
#include "edge/events.h"
#include "edge/modules.h"

static void test_ble_nav_basic(void **state) {
    (void)state;

    edge_event_t event_storage[8];
    edge_event_queue_t queue;
    assert_int_equal(edge_event_queue_init(&queue, event_storage, 8u), EDGE_OK);

    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    ble_nav_t nav;
    ble_nav_init(&nav, &sink);

    ble_nav_info_t info;
    assert_false(ble_nav_get_info(&nav, &info));

    /* Set navigation updates */
    ble_nav_set_flag(&nav, "turn-right", 10);
    ble_nav_set_narrative(&nav, "Turn right on Market St", 23);
    ble_nav_set_man_dist(&nav, "200 m", 5);
    ble_nav_set_progress(&nav, 45);

    assert_true(ble_nav_get_info(&nav, &info));
    assert_string_equal(info.flag, "turn-right");
    assert_string_equal(info.narrative, "Turn right on Market St");
    assert_string_equal(info.man_dist, "200 m");
    assert_int_equal(info.progress, 45);
    assert_true(edge_event_count(&queue) >= 4);

    edge_event_t ev;
    assert_int_equal(edge_event_pop(&queue, &ev), EDGE_OK);
    assert_int_equal(ev.id, EDGE_EVT_WATCH_NAV_UPDATED);

    /* Clear */
    ble_nav_clear(&nav);
    assert_false(ble_nav_get_info(&nav, &info));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ble_nav_basic),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
