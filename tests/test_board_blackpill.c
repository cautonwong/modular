/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "blackpill/board.h"
#include "edge/events.h"

static void test_board_blackpill_irq_events(void **state) {
    (void)state;
    edge_event_queue_t queue;
    edge_event_t queue_buf[8];
    edge_event_queue_init(&queue, queue_buf, 8);
    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    board_blackpill_init(&sink);

    board_blackpill_irq_key(true);
    board_blackpill_irq_matrix(1u, 2u, true);

    edge_event_t evt;
    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_WATCH_BUTTON);
    assert_int_equal(evt.arg0, 1u);

    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_ZMK_POSITION_STATE_CHANGED);
    assert_int_equal(evt.arg0, (1u << 16u) | 2u);
    assert_int_equal(evt.arg1, 1u);

    board_blackpill_set_led(true);

    board_blackpill_feed_watchdog();
    assert_int_equal(board_blackpill_watchdog_feeds(), 1);

    board_blackpill_enter_low_power();
    assert_int_equal(board_blackpill_low_power_entries(), 1);

    board_blackpill_system_reset();
    assert_int_equal(board_blackpill_reset_count(), 1);
}

static void test_board_blackpill_hw_initialization(void **state) {
    (void)state;
    board_blackpill_hw_t hw;
    assert_true(board_blackpill_hw_init(&hw));

    assert_int_equal(hw.rcc.ahb1enr & 0x07u, 0x07u);
    assert_int_equal(hw.gpioc.moder & (3u << 26u), (1u << 26u));
    assert_int_equal(hw.gpioa.moder & (3u << 0u), 0u);
    assert_int_equal(hw.spi1.cr1 & 0x0040u, 0x0040u);
    assert_int_equal(hw.i2c1.cr1, 0x0001u);
    assert_int_equal(hw.iwdg.kr, SOC_STM32F4_IWDG_KEY_RELOAD);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_blackpill_irq_events),
        cmocka_unit_test(test_board_blackpill_hw_initialization),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
