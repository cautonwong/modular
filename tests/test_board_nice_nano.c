/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "edge/events.h"
#include "nice_nano/board.h"

static void test_board_nice_nano_irq_events(void **state) {
    (void)state;
    edge_event_queue_t queue;
    edge_event_t queue_buf[8];
    edge_event_queue_init(&queue, queue_buf, 8);
    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    board_nice_nano_init(&sink);

    board_nice_nano_irq_matrix(2u, 3u, true);

    edge_event_t evt;
    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_ZMK_POSITION_STATE_CHANGED);
    assert_int_equal(evt.arg0, (2u << 16u) | 3u);
    assert_int_equal(evt.arg1, 1u);

    board_nice_nano_set_led(true);
    board_nice_nano_set_ext_power(true);

    board_nice_nano_feed_watchdog();
    assert_int_equal(board_nice_nano_watchdog_feeds(), 1);

    board_nice_nano_enter_low_power();
    assert_int_equal(board_nice_nano_low_power_entries(), 1);

    board_nice_nano_system_reset();
    assert_int_equal(board_nice_nano_reset_count(), 1);
}

static void test_board_nice_nano_hw_initialization(void **state) {
    (void)state;
    board_nice_nano_hw_t hw;
    assert_true(board_nice_nano_hw_init(&hw));

    assert_int_equal(hw.saadc_battery.enable, 1u);
    assert_int_equal(hw.saadc_battery.ch[0].pselp, 3u); /* AIN2 */

    assert_int_equal(hw.wdt_watchdog.tasks_start, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_nice_nano_irq_events),
        cmocka_unit_test(test_board_nice_nano_hw_initialization),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
