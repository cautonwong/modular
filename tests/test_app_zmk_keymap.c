/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_keymap/keymap.h"

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

typedef struct mock_layer_sink {
    int calls;
    uint8_t last_layer;
    bool last_active;
} mock_layer_sink_t;

static edge_status_t mock_post_layer(void *self, uint8_t layer, bool active) {
    mock_layer_sink_t *sink = (mock_layer_sink_t *)self;
    sink->calls++;
    sink->last_layer = layer;
    sink->last_active = active;
    return EDGE_OK;
}

static void test_keymap_layer_management(void **state) {
    (void)state;
    mock_behavior_sink_t bhv_sink = {0};
    zmk_keymap_behavior_if_t bhv_if = {.self = &bhv_sink, .invoke_binding = mock_invoke_binding};
    mock_layer_sink_t layer_sink = {0};
    zmk_keymap_event_sink_if_t layer_if = {.self = &layer_sink,
                                           .post_layer_event = mock_post_layer};

    zmk_keymap_app_t keymap;
    zmk_keymap_construct(&keymap, EDGE_MOD_ZMK_KEYMAP, 50, &bhv_if, &layer_if, 4, 32);
    assert_int_equal(zmk_keymap_init(&keymap), EDGE_OK);

    assert_true(zmk_keymap_layer_is_active(&keymap, 0));
    assert_false(zmk_keymap_layer_is_active(&keymap, 1));
    assert_int_equal(zmk_keymap_get_active_layers_mask(&keymap), 0x01);

    // Activate layer 1
    assert_int_equal(zmk_keymap_layer_activate(&keymap, 1), EDGE_OK);
    assert_true(zmk_keymap_layer_is_active(&keymap, 1));
    assert_int_equal(zmk_keymap_get_active_layers_mask(&keymap), 0x03);
    assert_int_equal(layer_sink.calls, 1);
    assert_int_equal(layer_sink.last_layer, 1);
    assert_true(layer_sink.last_active);

    // Toggle layer 2
    assert_int_equal(zmk_keymap_layer_toggle(&keymap, 2), EDGE_OK);
    assert_true(zmk_keymap_layer_is_active(&keymap, 2));
    assert_int_equal(zmk_keymap_get_active_layers_mask(&keymap), 0x07);

    // Toggle layer 2 off
    assert_int_equal(zmk_keymap_layer_toggle(&keymap, 2), EDGE_OK);
    assert_false(zmk_keymap_layer_is_active(&keymap, 2));
    assert_int_equal(zmk_keymap_get_active_layers_mask(&keymap), 0x03);

    // Deactivate layer 1
    assert_int_equal(zmk_keymap_layer_deactivate(&keymap, 1), EDGE_OK);
    assert_false(zmk_keymap_layer_is_active(&keymap, 1));
    assert_true(zmk_keymap_layer_is_active(&keymap, 0)); // default remains active
}

static void test_keymap_resolution_and_fallthrough(void **state) {
    (void)state;
    mock_behavior_sink_t bhv_sink = {0};
    zmk_keymap_behavior_if_t bhv_if = {.self = &bhv_sink, .invoke_binding = mock_invoke_binding};
    mock_layer_sink_t layer_sink = {0};
    zmk_keymap_event_sink_if_t layer_if = {.self = &layer_sink,
                                           .post_layer_event = mock_post_layer};

    zmk_keymap_app_t keymap;
    zmk_keymap_construct(&keymap, EDGE_MOD_ZMK_KEYMAP, 50, &bhv_if, &layer_if, 4, 32);
    assert_int_equal(zmk_keymap_init(&keymap), EDGE_OK);

    // Layer 0: Pos 0 -> Key A (0x04), Pos 1 -> Key B (0x05)
    zmk_keymap_set_binding(&keymap, 0, 0, (zmk_behavior_binding_t){ZMK_BHV_KEY_PRESS, 0x04, 0});
    zmk_keymap_set_binding(&keymap, 0, 1, (zmk_behavior_binding_t){ZMK_BHV_KEY_PRESS, 0x05, 0});

    // Layer 1: Pos 0 -> TRANS, Pos 1 -> Key C (0x06)
    zmk_keymap_set_binding(&keymap, 1, 0, (zmk_behavior_binding_t){ZMK_BHV_TRANS, 0, 0});
    zmk_keymap_set_binding(&keymap, 1, 1, (zmk_behavior_binding_t){ZMK_BHV_KEY_PRESS, 0x06, 0});

    // Press Pos 0 on Layer 0
    assert_int_equal(zmk_keymap_on_position_state_change(&keymap, 0, true, 100), EDGE_OK);
    assert_int_equal(bhv_sink.calls, 1);
    assert_int_equal(bhv_sink.last_behavior_id, ZMK_BHV_KEY_PRESS);
    assert_int_equal(bhv_sink.last_param1, 0x04);
    assert_true(bhv_sink.last_pressed);

    // Release Pos 0
    assert_int_equal(zmk_keymap_on_position_state_change(&keymap, 0, false, 110), EDGE_OK);
    assert_int_equal(bhv_sink.calls, 2);
    assert_false(bhv_sink.last_pressed);

    // Activate Layer 1
    zmk_keymap_layer_activate(&keymap, 1);

    // Press Pos 0 (Transparent on L1 -> should fall through to L0 Key A)
    assert_int_equal(zmk_keymap_on_position_state_change(&keymap, 0, true, 120), EDGE_OK);
    assert_int_equal(bhv_sink.calls, 3);
    assert_int_equal(bhv_sink.last_param1, 0x04);

    // Press Pos 1 (Override on L1 -> Key C 0x06)
    assert_int_equal(zmk_keymap_on_position_state_change(&keymap, 1, true, 130), EDGE_OK);
    assert_int_equal(bhv_sink.calls, 4);
    assert_int_equal(bhv_sink.last_param1, 0x06);

    // Deactivate Layer 1 while Pos 1 is held
    zmk_keymap_layer_deactivate(&keymap, 1);

    // Release Pos 1 -> MUST release Key C (0x06) from L1 because it was pressed on L1!
    assert_int_equal(zmk_keymap_on_position_state_change(&keymap, 1, false, 140), EDGE_OK);
    assert_int_equal(bhv_sink.calls, 5);
    assert_int_equal(bhv_sink.last_param1, 0x06);
    assert_false(bhv_sink.last_pressed);
}

static void test_conditional_layers(void **state) {
    (void)state;
    mock_behavior_sink_t bhv_sink = {0};
    zmk_keymap_behavior_if_t bhv_if = {.self = &bhv_sink, .invoke_binding = mock_invoke_binding};

    zmk_keymap_app_t keymap;
    zmk_keymap_construct(&keymap, EDGE_MOD_ZMK_KEYMAP, 50, &bhv_if, NULL, 4, 32);
    assert_int_equal(zmk_keymap_init(&keymap), EDGE_OK);

    // Condition: When Layer 1 (0x02) and Layer 2 (0x04) are BOTH active -> activate Layer 3
    assert_int_equal(zmk_keymap_add_conditional_layer(&keymap, (1u << 1) | (1u << 2), 3), EDGE_OK);

    zmk_keymap_layer_activate(&keymap, 1);
    assert_false(zmk_keymap_layer_is_active(&keymap, 3));

    zmk_keymap_layer_activate(&keymap, 2);
    // Now both 1 and 2 are active, so Layer 3 must be activated!
    assert_true(zmk_keymap_layer_is_active(&keymap, 3));

    // Deactivating layer 1 should also deactivate layer 3
    zmk_keymap_layer_deactivate(&keymap, 1);
    assert_false(zmk_keymap_layer_is_active(&keymap, 3));
    assert_true(zmk_keymap_layer_is_active(&keymap, 2));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_keymap_layer_management),
        cmocka_unit_test(test_keymap_resolution_and_fallthrough),
        cmocka_unit_test(test_conditional_layers),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
