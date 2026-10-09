/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/board_contract.h"
#include "edge/event.h"
#include "edge/events.h"
#include "keyboard_mock/board.h"

static void test_board_mock_inject_and_lifecycle(void **state) {
    (void)state;
    edge_event_t event_storage[8];
    edge_event_queue_t event_queue;
    assert_int_equal(edge_event_queue_init(&event_queue, event_storage, 8), EDGE_OK);

    edge_event_sink_t sink = {.queue = &event_queue, .clock = NULL, .guard = NULL};
    board_keyboard_mock_init(&sink);

    // Inject Key Row 1 Col 2 Press
    board_keyboard_mock_inject_key(1, 2, true);
    assert_int_equal(edge_event_count(&event_queue), 1);

    edge_event_t evt;
    assert_int_equal(edge_event_pop(&event_queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_ZMK_POSITION_STATE_CHANGED);
    assert_int_equal((uint8_t)(evt.arg0 & 0xFF), 1);
    assert_int_equal((uint8_t)((evt.arg0 >> 8) & 0xFF), 2);
    assert_int_equal((uint8_t)((evt.arg0 >> 16) & 0xFF), 1);

    // Hardware actions
    board_keyboard_mock_enter_low_power();
    assert_int_equal(board_keyboard_mock_low_power_entries(), 1);

    board_keyboard_mock_feed_watchdog();
    assert_int_equal(board_keyboard_mock_watchdog_feeds(), 1);

    board_keyboard_mock_system_reset();
    assert_int_equal(board_keyboard_mock_reset_count(), 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_mock_inject_and_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
