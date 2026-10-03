/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "kscan/kscan.h"

static uint8_t g_cb_row = 0;
static uint8_t g_cb_col = 0;
static bool g_cb_pressed = false;
static uint32_t g_cb_count = 0;

static void mock_kscan_callback(void *user_data, uint8_t row, uint8_t col, bool pressed) {
    (void)user_data;
    g_cb_row = row;
    g_cb_col = col;
    g_cb_pressed = pressed;
    g_cb_count++;
}

static void test_kscan_debounce_filter(void **state) {
    (void)state;
    g_cb_count = 0;

    const kscan_matrix_config_t cfg = {
        .rows = 4,
        .cols = 12,
        .debounce_press_ms = 5,
        .debounce_release_ms = 5,
    };

    kscan_matrix_t kscan;
    assert_int_equal(kscan_matrix_init(&kscan, &cfg, mock_kscan_callback, NULL), EDGE_OK);

    // Initial state: not pressed
    assert_false(kscan_matrix_is_pressed(&kscan, 1, 2));

    // Transient noise pulse (1ms at t=100) -> should NOT trigger callback
    kscan_matrix_feed_raw(&kscan, 1, 2, true, 100);
    kscan_matrix_process_debounce(&kscan, 101);
    assert_false(kscan_matrix_is_pressed(&kscan, 1, 2));
    assert_int_equal(g_cb_count, 0);

    // Noise goes away at t=102
    kscan_matrix_feed_raw(&kscan, 1, 2, false, 102);
    kscan_matrix_process_debounce(&kscan, 103);
    assert_false(kscan_matrix_is_pressed(&kscan, 1, 2));
    assert_int_equal(g_cb_count, 0);

    // Real stable press at t=110 held past debounce_press_ms (5ms) to t=116
    kscan_matrix_feed_raw(&kscan, 1, 2, true, 110);
    kscan_matrix_process_debounce(&kscan, 112); // hold 2ms -> not yet
    assert_false(kscan_matrix_is_pressed(&kscan, 1, 2));
    assert_int_equal(g_cb_count, 0);

    kscan_matrix_process_debounce(&kscan, 116); // hold 6ms -> stable!
    assert_true(kscan_matrix_is_pressed(&kscan, 1, 2));
    assert_int_equal(g_cb_count, 1);
    assert_int_equal(g_cb_row, 1);
    assert_int_equal(g_cb_col, 2);
    assert_true(g_cb_pressed);

    // Release at t=200, hold until t=206 (6ms) -> release callback
    kscan_matrix_feed_raw(&kscan, 1, 2, false, 200);
    kscan_matrix_process_debounce(&kscan, 202); // hold 2ms -> not yet
    assert_true(kscan_matrix_is_pressed(&kscan, 1, 2));
    assert_int_equal(g_cb_count, 1);

    kscan_matrix_process_debounce(&kscan, 206); // hold 6ms -> released!
    assert_false(kscan_matrix_is_pressed(&kscan, 1, 2));
    assert_int_equal(g_cb_count, 2);
    assert_false(g_cb_pressed);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_kscan_debounce_filter),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
