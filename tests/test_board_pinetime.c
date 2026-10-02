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
#include "pinetime/board.h"

static void test_board_pinetime_irq_events(void **state) {
    (void)state;
    edge_event_queue_t queue;
    edge_event_t queue_buf[8];
    edge_event_queue_init(&queue, queue_buf, 8);
    edge_event_sink_t sink = {
        .queue = &queue,
        .clock = NULL,
        .guard = NULL,
    };

    board_pinetime_init(&sink);

    board_pinetime_irq_button(true);
    board_pinetime_irq_touch();
    board_pinetime_irq_imu();

    edge_event_t evt;
    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_WATCH_BUTTON);
    assert_int_equal(evt.arg0, 1);

    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_WATCH_TOUCH);

    assert_int_equal(edge_event_pop(&queue, &evt), EDGE_OK);
    assert_int_equal(evt.id, EDGE_EVT_WATCH_WRIST_WAKE);

    board_pinetime_feed_watchdog();
    assert_int_equal(board_pinetime_watchdog_feeds(), 1);

    board_pinetime_enter_low_power();
    assert_int_equal(board_pinetime_low_power_entries(), 1);

    board_pinetime_system_reset();
    assert_int_equal(board_pinetime_reset_count(), 1);
}

static void test_board_pinetime_hw_initialization(void **state) {
    (void)state;
    board_pinetime_hw_t hw;
    assert_true(board_pinetime_hw_init(&hw));

    /* Verify SPIM0 for ST7789 */
    assert_int_equal(hw.spim_lcd.enable, 7u);
    assert_int_equal(hw.spim_lcd.psel_sck, PINETIME_PIN_LCD_SCK);
    assert_int_equal(hw.spim_lcd.psel_mosi, PINETIME_PIN_LCD_MOSI);

    /* Verify SPIM1 for XT25F32B Flash */
    assert_int_equal(hw.spim_flash.enable, 7u);
    assert_int_equal(hw.spim_flash.psel_sck, PINETIME_PIN_LCD_SCK);

    /* Verify TWIM1 for Sensors */
    assert_int_equal(hw.twim_sensors.enable, 6u);
    assert_int_equal(hw.twim_sensors.psel_scl, PINETIME_PIN_TOUCH_SCL);
    assert_int_equal(hw.twim_sensors.psel_sda, PINETIME_PIN_TOUCH_SDA);

    /* Verify SAADC for Battery */
    assert_int_equal(hw.saadc_battery.enable, 1u);
    assert_int_equal(hw.saadc_battery.ch[0].pselp, 8u); /* AIN7 */

    /* Verify Watchdog */
    assert_int_equal(hw.wdt_watchdog.tasks_start, 1u);
    board_pinetime_feed_watchdog();
    assert_int_equal(hw.wdt_watchdog.rr[0], SOC_NRF52_WDT_RR_VALUE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_board_pinetime_irq_events),
        cmocka_unit_test(test_board_pinetime_hw_initialization),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
