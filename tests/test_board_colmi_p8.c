/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "colmi_p8/board.h"
#include "edge/events.h"

static void test_board_colmi_p8_irq_events(void **state) {
    (void)state;
    edge_event_queue_t queue;
    edge_event_t queue_buf[8];
    edge_event_queue_init(&queue, queue_buf, 8);
    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    board_colmi_p8_init(&sink);

    board_colmi_p8_irq_button(true);
    board_colmi_p8_irq_touch();

    edge_event_t evt;
    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_WATCH_BUTTON);
    assert_int_equal(evt.arg0, 1);

    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_WATCH_TOUCH);

    board_colmi_p8_feed_watchdog();
    assert_int_equal(board_colmi_p8_watchdog_feeds(), 1);

    board_colmi_p8_enter_low_power();
    assert_int_equal(board_colmi_p8_low_power_entries(), 1);

    board_colmi_p8_system_reset();
    assert_int_equal(board_colmi_p8_reset_count(), 1);
}

static void test_board_colmi_p8_hw_initialization(void **state) {
    (void)state;
    board_colmi_p8_hw_t hw;
    assert_true(board_colmi_p8_hw_init(&hw));

    assert_int_equal(hw.spim_lcd.enable, 7u);
    assert_int_equal(hw.spim_lcd.psel_sck, COLMI_P8_PIN_LCD_SCK);
    assert_int_equal(hw.spim_lcd.psel_mosi, COLMI_P8_PIN_LCD_MOSI);

    assert_int_equal(hw.twim_touch.enable, 6u);
    assert_int_equal(hw.twim_touch.psel_scl, COLMI_P8_PIN_TOUCH_SCL);
    assert_int_equal(hw.twim_touch.psel_sda, COLMI_P8_PIN_TOUCH_SDA);

    assert_int_equal(hw.saadc_battery.enable, 1u);
    assert_int_equal(hw.saadc_battery.ch[0].pselp, 8u);

    assert_int_equal(hw.wdt_watchdog.tasks_start, 1u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_colmi_p8_irq_events),
        cmocka_unit_test(test_board_colmi_p8_hw_initialization),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
