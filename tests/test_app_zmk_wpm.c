/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_wpm/wpm.h"

typedef struct mock_wpm_sink {
    int calls;
    uint8_t last_wpm;
} mock_wpm_sink_t;

static edge_status_t mock_on_wpm(void *self, uint8_t wpm) {
    mock_wpm_sink_t *sink = (mock_wpm_sink_t *)self;
    sink->calls++;
    sink->last_wpm = wpm;
    return EDGE_OK;
}

static void test_wpm_keystrokes_and_decay(void **state) {
    (void)state;
    mock_wpm_sink_t sink_ctx = {0};
    zmk_wpm_sink_if_t sink_if = {.self = &sink_ctx, .on_wpm_state_changed = mock_on_wpm};

    zmk_wpm_app_t app;
    zmk_wpm_construct(&app, EDGE_MOD_ZMK_WPM, 50, &sink_if);
    assert_int_equal(zmk_wpm_init(&app), EDGE_OK);

    assert_int_equal(zmk_wpm_get_current_wpm(&app), 0);

    // Type 25 characters at t=1000
    for (int i = 0; i < 25; i++) {
        zmk_wpm_record_keystroke(&app, 1000);
    }

    // 25 chars in 5s = (25 * 12) / 5 = 60 WPM
    assert_int_equal(zmk_wpm_get_current_wpm(&app), 60);
    assert_int_equal(sink_ctx.last_wpm, 60);

    // Fast forward 6 seconds (6000ms) -> entire window clears -> WPM decays to 0
    zmk_wpm_tick(&app, 7000);
    assert_int_equal(zmk_wpm_get_current_wpm(&app), 0);
    assert_int_equal(sink_ctx.last_wpm, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_wpm_keystrokes_and_decay),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
