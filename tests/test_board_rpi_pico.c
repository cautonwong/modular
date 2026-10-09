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
#include "rpi_pico/board.h"

static void test_board_rpi_pico_irq_events(void **state) {
    (void)state;
    edge_event_queue_t queue;
    edge_event_t queue_buf[8];
    edge_event_queue_init(&queue, queue_buf, 8);
    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    board_rpi_pico_init(&sink);

    board_rpi_pico_irq_matrix(2u, 1u, true);

    edge_event_t evt;
    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_ZMK_POSITION_STATE_CHANGED);
    assert_int_equal(evt.arg0, (2u << 16u) | 1u);
    assert_int_equal(evt.arg1, 1u);

    board_rpi_pico_set_led(true);

    board_rpi_pico_feed_watchdog();
    assert_int_equal(board_rpi_pico_watchdog_feeds(), 1);

    board_rpi_pico_enter_low_power();
    assert_int_equal(board_rpi_pico_low_power_entries(), 1);

    board_rpi_pico_system_reset();
    assert_int_equal(board_rpi_pico_reset_count(), 1);
}

static void test_board_rpi_pico_hw_initialization(void **state) {
    (void)state;
    board_rpi_pico_hw_t hw;
    assert_true(board_rpi_pico_hw_init(&hw));

    assert_int_equal(hw.sio.gpio_oe & (1u << RPI_PICO_PIN_LED), (1u << RPI_PICO_PIN_LED));
    assert_int_equal(hw.spi.sspcr1, 0x02u);
    assert_int_equal(hw.i2c.ic_enable, 1u);
    assert_int_equal(hw.wdt.ctrl & 0x40000000u, 0x40000000u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_rpi_pico_irq_events),
        cmocka_unit_test(test_board_rpi_pico_hw_initialization),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
