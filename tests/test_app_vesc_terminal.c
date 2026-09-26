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

/*
 * The paths the command test does not reach: the module's own hooks, the construct/init guards,
 * the faults branch that has something to report (which is where the hex formatter runs), the
 * unknown-command fallback, help, and the stats branch for a system port with no get_stats - which
 * says so rather than reading through a null callback.
 */
static void test_terminal_edges_and_guards(void **state) {
    (void)state;
    vesc_terminal_app_t app;
    mock_terminal_hw_t hw = {
        .rpm = 3500.0f, .iq = 12.5f, .v_bus = 48.0f, .temp = 38.5f, .faults = 0x00000042u};
    terminal_stream_port_t stream = {.self = &hw, .write_string = mock_write_str};
    terminal_system_port_t sys = {.self = &hw, .get_stats = mock_get_stats};

    /* Guards: a null app is a no-op, and init insists on a stream port. */
    vesc_terminal_construct(NULL, EDGE_MOD_VESC_TERMINAL, 50u, &stream, &sys);
    terminal_system_port_t no_stream_ok = sys;
    (void)no_stream_ok;
    vesc_terminal_app_t no_stream;
    vesc_terminal_construct(&no_stream, EDGE_MOD_VESC_TERMINAL, 50u, NULL, &sys);
    assert_int_equal(vesc_terminal_init(&no_stream), EDGE_EINVAL);

    vesc_terminal_construct(&app, EDGE_MOD_VESC_TERMINAL, 50u, &stream, &sys);
    assert_int_equal(vesc_terminal_init(&app), EDGE_OK);
    assert_ptr_equal(vesc_terminal_module(&app), &app.module);
    assert_ptr_equal(vesc_terminal_module(NULL), NULL);

    /* The module's own hooks answer, and power_off is the safe state. */
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);

    /* A fault code has something to report, and the hex formatter runs for it. */
    assert_int_equal(vesc_terminal_execute(&app, "faults"), EDGE_OK);
    assert_non_null(strstr(hw.last_tx, "Fault code: 0x00000042"));

    assert_int_equal(vesc_terminal_execute(&app, "help"), EDGE_OK);
    assert_true(strlen(hw.last_tx) > 0u);

    assert_int_equal(vesc_terminal_execute(&app, "not-a-command"), EDGE_OK);
    assert_non_null(strstr(hw.last_tx, "Unknown command"));

    /* A system port with no get_stats: the terminal says so instead of calling through null. */
    terminal_system_port_t bare = {.self = &hw, .get_stats = NULL};
    vesc_terminal_app_t bare_app;
    vesc_terminal_construct(&bare_app, EDGE_MOD_VESC_TERMINAL, 50u, &stream, &bare);
    assert_int_equal(vesc_terminal_init(&bare_app), EDGE_OK);
    assert_int_equal(vesc_terminal_execute(&bare_app, "stats"), EDGE_OK);
    assert_int_equal(vesc_terminal_execute(&bare_app, "faults"), EDGE_OK);
    assert_non_null(strstr(hw.last_tx, "unavailable"));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_terminal_commands),
        cmocka_unit_test(test_terminal_edges_and_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
