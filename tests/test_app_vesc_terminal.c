/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "vesc_terminal/vesc_terminal.h"

typedef struct mock_terminal_hw {
    char last_tx[256];
    float rpm;
    float iq;
    float v_bus;
    float temp;
    uint32_t faults;
} mock_terminal_hw_t;

static edge_status_t mock_write_str(void *self, const char *str) {
    mock_terminal_hw_t *hw = (mock_terminal_hw_t *)self;
    strncpy(hw->last_tx, str, sizeof(hw->last_tx) - 1);
    return EDGE_OK;
}

static edge_status_t mock_get_stats(void *self, float *rpm, float *iq, float *v_bus, float *temp,
                                    uint32_t *faults) {
    mock_terminal_hw_t *hw = (mock_terminal_hw_t *)self;
    *rpm = hw->rpm;
    *iq = hw->iq;
    *v_bus = hw->v_bus;
    *temp = hw->temp;
    *faults = hw->faults;
    return EDGE_OK;
}

static void test_terminal_commands(void **state) {
    (void)state;
    vesc_terminal_app_t app;
    mock_terminal_hw_t hw = {
        .rpm = 3500.0f,
        .iq = 12.5f,
        .v_bus = 48.0f,
        .temp = 38.5f,
        .faults = 0,
    };
    terminal_stream_port_t stream = {.self = &hw, .write_string = mock_write_str};
    terminal_system_port_t sys = {.self = &hw, .get_stats = mock_get_stats};

    vesc_terminal_construct(&app, EDGE_MOD_VESC_TERMINAL, 50u, &stream, &sys);
    assert_int_equal(vesc_terminal_init(&app), EDGE_OK);

    assert_int_equal(vesc_terminal_execute(&app, "ping"), EDGE_OK);
    assert_string_equal(hw.last_tx, "pong\r\n");

    assert_int_equal(vesc_terminal_execute(&app, "faults"), EDGE_OK);
    assert_string_equal(hw.last_tx, "No faults\r\n");

    assert_int_equal(vesc_terminal_execute(&app, "stats"), EDGE_OK);
    assert_non_null(strstr(hw.last_tx, "RPM: 3500.0"));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_terminal_commands),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
