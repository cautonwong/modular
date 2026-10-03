/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "st7789/st7789.h"

static void test_st7789_window_valid(void **state) {
    (void)state;
    st7789_window_cmds_t cmds;
    assert_int_equal(st7789_format_window(10, 20, 100, 200, &cmds), EDGE_OK);

    assert_int_equal(cmds.caset_cmd, ST7789_CMD_CASET);
    assert_int_equal(cmds.caset_data[0], 0);
    assert_int_equal(cmds.caset_data[1], 10);
    assert_int_equal(cmds.caset_data[2], 0);
    assert_int_equal(cmds.caset_data[3], 100);

    assert_int_equal(cmds.raset_cmd, ST7789_CMD_RASET);
    assert_int_equal(cmds.raset_data[0], 0);
    assert_int_equal(cmds.raset_data[1], 20);
    assert_int_equal(cmds.raset_data[2], 0);
    assert_int_equal(cmds.raset_data[3], 200);

    assert_int_equal(cmds.ramwr_cmd, ST7789_CMD_RAMWR);
}

static void test_st7789_window_invalid(void **state) {
    (void)state;
    st7789_window_cmds_t cmds;
    assert_int_equal(st7789_format_window(240, 0, 200, 200, &cmds), EDGE_EINVAL);
    assert_int_equal(st7789_format_window(100, 0, 50, 200, &cmds), EDGE_EINVAL);
    assert_int_equal(st7789_format_window(0, 0, 10, 10, NULL), EDGE_EINVAL);
}

static void test_st7789_scroll_formatting(void **state) {
    (void)state;
    st7789_scroll_cmd_t cmd;
    assert_int_equal(st7789_format_scroll(80, &cmd), EDGE_OK);
    assert_int_equal(cmd.cmd, ST7789_CMD_VSCRSADD);
    assert_int_equal(cmd.data[0], 0);
    assert_int_equal(cmd.data[1], 80);

    assert_int_equal(st7789_format_scroll(320, &cmd), EDGE_EINVAL);
    assert_int_equal(st7789_format_scroll(0, NULL), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_st7789_window_valid),
        cmocka_unit_test(test_st7789_window_invalid),
        cmocka_unit_test(test_st7789_scroll_formatting),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
