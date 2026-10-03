/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_combo/combo.h"

#define ZMK_BHV_KEY_PRESS 2

typedef struct mock_behavior_sink {
    int calls;
    uint16_t last_behavior_id;
    uint32_t last_param1;
    uint32_t last_param2;
    bool last_pressed;
    uint32_t last_time;
} mock_behavior_sink_t;

static edge_status_t mock_invoke_binding(void *self, uint16_t behavior_id, uint32_t param1,
                                         uint32_t param2, bool pressed, uint32_t timestamp_ms) {
    mock_behavior_sink_t *sink = (mock_behavior_sink_t *)self;
    sink->calls++;
    sink->last_behavior_id = behavior_id;
    sink->last_param1 = param1;
    sink->last_param2 = param2;
    sink->last_pressed = pressed;
    sink->last_time = timestamp_ms;
    return EDGE_OK;
}

static void test_combo_trigger_and_release(void **state) {
    (void)state;
    mock_behavior_sink_t bhv_sink = {0};
    zmk_combo_behavior_if_t bhv_if = {.self = &bhv_sink, .invoke_binding = mock_invoke_binding};

    zmk_combo_app_t combo;
    zmk_combo_construct(&combo, EDGE_MOD_ZMK_COMBO, 50, &bhv_if);
    assert_int_equal(zmk_combo_init(&combo), EDGE_OK);

    // Define combo: Pos 1 + Pos 2 -> Key ESC (0x29) within 50ms
    zmk_combo_config_t cfg = {
        .positions = {1, 2},
        .position_count = 2,
        .layers_mask = 0,
        .timeout_ms = 50,
        .binding = {ZMK_BHV_KEY_PRESS, 0x29, 0},
    };
    assert_int_equal(zmk_combo_add_combo(&combo, &cfg), EDGE_OK);

    // Press Pos 1 at t=100
    bool absorbed = zmk_combo_process_key(&combo, 1, true, 100, 0x01);
    assert_false(absorbed); // first key not full chord yet

    // Press Pos 2 at t=120 (delta 20ms <= 50ms) -> full chord!
    absorbed = zmk_combo_process_key(&combo, 2, true, 120, 0x01);
    assert_true(absorbed);
    assert_int_equal(bhv_sink.calls, 1);
    assert_int_equal(bhv_sink.last_behavior_id, ZMK_BHV_KEY_PRESS);
    assert_int_equal(bhv_sink.last_param1, 0x29);
    assert_true(bhv_sink.last_pressed);

    // Release Pos 1 at t=180 -> combo released
    absorbed = zmk_combo_process_key(&combo, 1, false, 180, 0x01);
    assert_true(absorbed);
    assert_int_equal(bhv_sink.calls, 2);
    assert_false(bhv_sink.last_pressed);
}

static void test_combo_timeout_does_not_fire(void **state) {
    (void)state;
    mock_behavior_sink_t bhv_sink = {0};
    zmk_combo_behavior_if_t bhv_if = {.self = &bhv_sink, .invoke_binding = mock_invoke_binding};

    zmk_combo_app_t combo;
    zmk_combo_construct(&combo, EDGE_MOD_ZMK_COMBO, 50, &bhv_if);
    assert_int_equal(zmk_combo_init(&combo), EDGE_OK);

    zmk_combo_config_t cfg = {
        .positions = {1, 2},
        .position_count = 2,
        .layers_mask = 0,
        .timeout_ms = 40,
        .binding = {ZMK_BHV_KEY_PRESS, 0x29, 0},
    };
    zmk_combo_add_combo(&combo, &cfg);

    // Press Pos 1 at t=100
    zmk_combo_process_key(&combo, 1, true, 100, 0x01);

    // Press Pos 2 at t=160 (delta 60ms > 40ms timeout) -> combo timed out!
    bool absorbed = zmk_combo_process_key(&combo, 2, true, 160, 0x01);
    assert_false(absorbed);
    assert_int_equal(bhv_sink.calls, 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_combo_trigger_and_release),
        cmocka_unit_test(test_combo_timeout_does_not_fire),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
