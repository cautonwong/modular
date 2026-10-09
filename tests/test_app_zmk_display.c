/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_display/display.h"

typedef struct mock_display_hw {
    int calls;
    char line1[32];
    char line2[32];
    char line3[32];
    char line4[32];
} mock_display_hw_t;

static edge_status_t mock_draw_screen(void *self, const char *line1, const char *line2,
                                      const char *line3, const char *line4) {
    mock_display_hw_t *h = (mock_display_hw_t *)self;
    h->calls++;
    strncpy(h->line1, line1, sizeof(h->line1) - 1);
    strncpy(h->line2, line2, sizeof(h->line2) - 1);
    strncpy(h->line3, line3, sizeof(h->line3) - 1);
    strncpy(h->line4, line4, sizeof(h->line4) - 1);
    return EDGE_OK;
}

static void test_display_widget_updates_and_sleep(void **state) {
    (void)state;
    mock_display_hw_t hw_ctx = {0};
    zmk_display_hw_if_t hw = {.self = &hw_ctx, .draw_screen = mock_draw_screen};

    zmk_display_app_t app;
    zmk_display_construct(&app, EDGE_MOD_ZMK_DISPLAY, 70, &hw);
    assert_int_equal(zmk_display_init(&app), EDGE_OK);

    assert_int_equal(hw_ctx.calls, 1);
    assert_string_equal(hw_ctx.line1, "OUT: BLE-CONN (PRF 1)");
    assert_string_equal(hw_ctx.line2, "LAYER: DEF [0]");
    assert_string_equal(hw_ctx.line3, "BAT: 100%");
    assert_string_equal(hw_ctx.line4, "WPM: 0");

    // Update battery to 85% + USB charging
    assert_int_equal(zmk_display_on_battery_state(&app, 85, true), EDGE_OK);
    // Update layer to NAV (layer 2)
    assert_int_equal(zmk_display_on_layer_state(&app, 2, "NAV"), EDGE_OK);
    // Update WPM to 72
    assert_int_equal(zmk_display_on_wpm_state(&app, 72), EDGE_OK);

    assert_int_equal(zmk_display_update(&app), EDGE_OK);
    assert_string_equal(hw_ctx.line1, "OUT: USB (PRF 1)");
    assert_string_equal(hw_ctx.line2, "LAYER: NAV [2]");
    assert_string_equal(hw_ctx.line3, "BAT: CHG (85%)");
    assert_string_equal(hw_ctx.line4, "WPM: 72");

    // Go to sleep
    assert_int_equal(zmk_display_on_activity_state(&app, 2 /* SLEEP */), EDGE_OK);
    assert_int_equal(zmk_display_update(&app), EDGE_OK);
    assert_string_equal(hw_ctx.line2, "   [SLEEP]   ");
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_display_widget_updates_and_sleep),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
