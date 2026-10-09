/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_behavior/behavior.h"

typedef struct mock_hid {
    int press_calls;
    int release_calls;
    uint8_t last_pressed_kc;
    uint8_t last_pressed_mods;
    uint8_t last_released_kc;
    uint8_t last_released_mods;
    uint8_t last_mouse_button;
    bool mouse_pressed;
} mock_hid_t;

static edge_status_t mock_press_key(void *self, uint8_t keycode, uint8_t modifiers) {
    mock_hid_t *hid = (mock_hid_t *)self;
    hid->press_calls++;
    hid->last_pressed_kc = keycode;
    hid->last_pressed_mods = modifiers;
    return EDGE_OK;
}

static edge_status_t mock_release_key(void *self, uint8_t keycode, uint8_t modifiers) {
    mock_hid_t *hid = (mock_hid_t *)self;
    hid->release_calls++;
    hid->last_released_kc = keycode;
    hid->last_released_mods = modifiers;
    return EDGE_OK;
}

static edge_status_t mock_press_consumer(void *self, uint16_t code) {
    (void)self;
    (void)code;
    return EDGE_OK;
}

static edge_status_t mock_release_consumer(void *self, uint16_t code) {
    (void)self;
    (void)code;
    return EDGE_OK;
}

static edge_status_t mock_press_mouse(void *self, uint8_t button) {
    mock_hid_t *hid = (mock_hid_t *)self;
    hid->mouse_pressed = true;
    hid->last_mouse_button = button;
    return EDGE_OK;
}

static edge_status_t mock_release_mouse(void *self, uint8_t button) {
    mock_hid_t *hid = (mock_hid_t *)self;
    hid->mouse_pressed = false;
    hid->last_mouse_button = button;
    return EDGE_OK;
}

static void test_behavior_key_press_and_repeat(void **state) {
    (void)state;
    mock_hid_t hid_ctx = {0};
    zmk_behavior_hid_if_t hid_if = {
        .self = &hid_ctx,
        .press_key = mock_press_key,
        .release_key = mock_release_key,
        .press_consumer_key = mock_press_consumer,
        .release_consumer_key = mock_release_consumer,
        .press_mouse_button = mock_press_mouse,
        .release_mouse_button = mock_release_mouse,
    };

    zmk_behavior_app_t app;
    zmk_behavior_construct(&app, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&app), EDGE_OK);

    // Press Key A (0x04)
    assert_int_equal(zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x04, 0, true, 100), EDGE_OK);
    assert_int_equal(hid_ctx.press_calls, 1);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x04);

    // Release Key A
    assert_int_equal(zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x04, 0, false, 120), EDGE_OK);
    assert_int_equal(hid_ctx.release_calls, 1);
    assert_int_equal(hid_ctx.last_released_kc, 0x04);

    // Key repeat -> should repeat Key A
    assert_int_equal(zmk_behavior_invoke(&app, ZMK_BHV_KEY_REPEAT, 0, 0, true, 150), EDGE_OK);
    assert_int_equal(hid_ctx.press_calls, 2);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x04);
}

static void test_behavior_hold_tap(void **state) {
    (void)state;
    mock_hid_t hid_ctx = {0};
    zmk_behavior_hid_if_t hid_if = {
        .self = &hid_ctx,
        .press_key = mock_press_key,
        .release_key = mock_release_key,
        .press_consumer_key = mock_press_consumer,
        .release_consumer_key = mock_release_consumer,
        .press_mouse_button = mock_press_mouse,
        .release_mouse_button = mock_release_mouse,
    };

    zmk_behavior_app_t app;
    zmk_behavior_construct(&app, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&app), EDGE_OK);

    // Setup Hold-Tap: Tap = Key A (0x04), Hold = Key LCtrl (mod 0x01 on key 0xE0)
    // Tapping term = 200ms
    zmk_ht_config_t ht_cfg = {
        .flavor = ZMK_HT_HOLD_PREFERRED,
        .tapping_term_ms = 200,
        .quick_tap_ms = 0,
        .retro_tap = false,
    };
    uint8_t ht_idx = 0;
    zmk_behavior_add_hold_tap(&app, &ht_cfg, ZMK_BHV_KEY_PRESS, 0xE0, ZMK_BHV_KEY_PRESS, 0x04,
                              &ht_idx);

    // Scenario 1: Tap within 100ms (< 200ms)
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, ht_idx, 0, true, 100);
    assert_int_equal(hid_ctx.press_calls, 0); // Not decided yet

    // Release at 150ms -> fires Tap (Key A)
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, ht_idx, 0, false, 150);
    assert_int_equal(hid_ctx.press_calls, 1);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x04);
    assert_int_equal(hid_ctx.release_calls, 1);
    assert_int_equal(hid_ctx.last_released_kc, 0x04);

    // Scenario 2: Hold past 200ms
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, ht_idx, 0, true, 300);
    // Tick at 550ms (delta 250ms >= 200ms)
    zmk_behavior_tick(&app, 550);
    assert_int_equal(hid_ctx.press_calls, 2);
    assert_int_equal(hid_ctx.last_pressed_kc, 0xE0); // Hold key LCtrl fired!

    // Release hold
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, ht_idx, 0, false, 600);
    assert_int_equal(hid_ctx.release_calls, 2);
    assert_int_equal(hid_ctx.last_released_kc, 0xE0);
}

static void test_behavior_tap_dance(void **state) {
    (void)state;
    mock_hid_t hid_ctx = {0};
    zmk_behavior_hid_if_t hid_if = {
        .self = &hid_ctx,
        .press_key = mock_press_key,
        .release_key = mock_release_key,
        .press_consumer_key = mock_press_consumer,
        .release_consumer_key = mock_release_consumer,
        .press_mouse_button = mock_press_mouse,
        .release_mouse_button = mock_release_mouse,
    };

    zmk_behavior_app_t app;
    zmk_behavior_construct(&app, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&app), EDGE_OK);

    // Tap Dance: 1 tap = ESC (0x29), 2 taps = Caps Lock (0x39)
    zmk_tap_dance_config_t td_cfg = {
        .tapping_term_ms = 200,
        .bindings =
            {
                {ZMK_BHV_KEY_PRESS, 0x29},
                {ZMK_BHV_KEY_PRESS, 0x39},
            },
        .binding_count = 2,
    };
    uint8_t td_idx = 0;
    zmk_behavior_add_tap_dance(&app, &td_cfg, &td_idx);

    // Double tap
    zmk_behavior_invoke(&app, ZMK_BHV_TAP_DANCE, td_idx, 0, true, 100);
    zmk_behavior_invoke(&app, ZMK_BHV_TAP_DANCE, td_idx, 0, false, 120);
    zmk_behavior_invoke(&app, ZMK_BHV_TAP_DANCE, td_idx, 0, true, 150);
    // On 2nd tap (max count), immediately fires Caps Lock (0x39)
    assert_int_equal(hid_ctx.press_calls, 1);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x39);

    zmk_behavior_invoke(&app, ZMK_BHV_TAP_DANCE, td_idx, 0, false, 170);
    assert_int_equal(hid_ctx.release_calls, 1);
    assert_int_equal(hid_ctx.last_released_kc, 0x39);
}

static void test_behavior_sticky_key_and_mod_morph(void **state) {
    (void)state;
    mock_hid_t hid_ctx = {0};
    zmk_behavior_hid_if_t hid_if = {
        .self = &hid_ctx,
        .press_key = mock_press_key,
        .release_key = mock_release_key,
        .press_consumer_key = mock_press_consumer,
        .release_consumer_key = mock_release_consumer,
        .press_mouse_button = mock_press_mouse,
        .release_mouse_button = mock_release_mouse,
    };

    zmk_behavior_app_t app;
    zmk_behavior_construct(&app, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&app), EDGE_OK);

    // Mod Morph: Backspace (0x2A) morphed to Delete (0x4C) when Shift (0x02) is active
    zmk_mod_morph_config_t morph_cfg = {
        .default_keycode = 0x2A,
        .default_mods = 0,
        .morphed_keycode = 0x4C,
        .morphed_mods = 0,
        .trigger_mods_mask = 0x02,
    };
    uint8_t morph_idx = 0;
    zmk_behavior_add_mod_morph(&app, &morph_cfg, &morph_idx);

    // Press Backspace without shift -> fires Backspace (0x2A)
    zmk_behavior_invoke(&app, ZMK_BHV_MOD_MORPH, morph_idx, 0, true, 100);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x2A);
    zmk_behavior_invoke(&app, ZMK_BHV_MOD_MORPH, morph_idx, 0, false, 110);

    // Trigger Sticky Shift (0x02)
    zmk_behavior_invoke(&app, ZMK_BHV_STICKY_KEY, 0x02, 1000, true, 200);

    // Press Backspace -> because Sticky Shift is active, it morphs into Delete (0x4C)!
    zmk_behavior_invoke(&app, ZMK_BHV_MOD_MORPH, morph_idx, 0, true, 220);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x4C);
    zmk_behavior_invoke(&app, ZMK_BHV_MOD_MORPH, morph_idx, 0, false, 230);
}

static void test_behavior_caps_word(void **state) {
    (void)state;
    mock_hid_t hid_ctx = {0};
    zmk_behavior_hid_if_t hid_if = {
        .self = &hid_ctx,
        .press_key = mock_press_key,
        .release_key = mock_release_key,
        .press_consumer_key = mock_press_consumer,
        .release_consumer_key = mock_release_consumer,
        .press_mouse_button = mock_press_mouse,
        .release_mouse_button = mock_release_mouse,
    };

    zmk_behavior_app_t app;
    zmk_behavior_construct(&app, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&app), EDGE_OK);

    // Enable Caps Word
    zmk_behavior_invoke(&app, ZMK_BHV_CAPS_WORD, 0, 0, true, 100);

    // Type 'a' (0x04) -> should have Left Shift (0x02) applied automatically!
    zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x04, 0, true, 110);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x04);
    assert_int_equal(hid_ctx.last_pressed_mods, 0x02);
    zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x04, 0, false, 120);

    // Type Space (0x2C) -> should deactivate Caps Word
    zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x2C, 0, true, 130);
    zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x2C, 0, false, 140);

    // Type 'b' (0x05) -> Caps Word is now off, so mods should be 0
    zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x05, 0, true, 150);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x05);
    assert_int_equal(hid_ctx.last_pressed_mods, 0x00);
    zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x05, 0, false, 160);
}

static void test_behavior_hold_tap_flavors_and_retro(void **state) {
    (void)state;
    mock_hid_t hid_ctx = {0};
    zmk_behavior_hid_if_t hid_if = {
        .self = &hid_ctx,
        .press_key = mock_press_key,
        .release_key = mock_release_key,
        .press_consumer_key = mock_press_consumer,
        .release_consumer_key = mock_release_consumer,
        .press_mouse_button = mock_press_mouse,
        .release_mouse_button = mock_release_mouse,
    };

    zmk_behavior_app_t app;
    zmk_behavior_construct(&app, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&app), EDGE_OK);

    // 1. Retro-tap: when held past tapping term with NO other key pressed -> tap on release
    zmk_ht_config_t retro_cfg = {
        .flavor = ZMK_HT_HOLD_PREFERRED,
        .tapping_term_ms = 200,
        .quick_tap_ms = 0,
        .retro_tap = true,
    };
    uint8_t retro_idx = 0;
    zmk_behavior_add_hold_tap(&app, &retro_cfg, ZMK_BHV_KEY_PRESS, 0xE0, ZMK_BHV_KEY_PRESS, 0x04,
                              &retro_idx);

    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, retro_idx, 0, true, 100);
    zmk_behavior_tick(&app, 350); // Timeout expires -> becomes hold
    // Release with no other keys -> retro-tap should fire Tap (0x04)
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, retro_idx, 0, false, 400);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x04);

    // 2. Quick-tap: first press at timestamp < quick_tap_ms must NOT be misidentified as double tap
    zmk_ht_config_t qtap_cfg = {
        .flavor = ZMK_HT_HOLD_PREFERRED,
        .tapping_term_ms = 200,
        .quick_tap_ms = 150,
        .retro_tap = false,
    };
    uint8_t qtap_idx = 0;
    zmk_behavior_add_hold_tap(&app, &qtap_cfg, ZMK_BHV_KEY_PRESS, 0xE0, ZMK_BHV_KEY_PRESS, 0x05,
                              &qtap_idx);

    // Initial press at 50ms (< 150ms): should NOT fire tap immediately because has_previous_tap is
    // false
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, qtap_idx, 0, true, 50);
    assert_false(app.hold_taps[qtap_idx].is_tapped);
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, qtap_idx, 0, false, 80);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x05); // Tapped on release

    // Second press at 120ms (delta 40ms < 150ms quick_tap_ms) -> now has_previous_tap is true, so
    // fires tap immediately
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, qtap_idx, 0, true, 120);
    assert_true(app.hold_taps[qtap_idx].is_tapped);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x05);
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, qtap_idx, 0, false, 150);

    // 3. TAP_UNLESS_INTERRUPTED: holding past term does NOT hold unless interrupted
    zmk_ht_config_t tui_cfg = {
        .flavor = ZMK_HT_TAP_UNLESS_INTERRUPTED,
        .tapping_term_ms = 200,
        .quick_tap_ms = 0,
        .retro_tap = false,
    };
    uint8_t tui_idx = 0;
    zmk_behavior_add_hold_tap(&app, &tui_cfg, ZMK_BHV_KEY_PRESS, 0xE0, ZMK_BHV_KEY_PRESS, 0x06,
                              &tui_idx);

    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, tui_idx, 0, true, 700);
    zmk_behavior_tick(&app, 950); // Past term, no other key -> stays inactive hold
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, tui_idx, 0, false, 1000);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x06); // Fires tap (0x06)

    // Interrupted -> immediately holds
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, tui_idx, 0, true, 1100);
    zmk_behavior_invoke(&app, ZMK_BHV_KEY_PRESS, 0x07, 0, true, 1150); // Interrupted by key 0x07
    zmk_behavior_invoke(&app, ZMK_BHV_HOLD_TAP, tui_idx, 0, false, 1200);
}

static void test_behavior_macro_and_key_toggle(void **state) {
    (void)state;
    mock_hid_t hid_ctx = {0};
    zmk_behavior_hid_if_t hid_if = {
        .self = &hid_ctx,
        .press_key = mock_press_key,
        .release_key = mock_release_key,
        .press_consumer_key = mock_press_consumer,
        .release_consumer_key = mock_release_consumer,
        .press_mouse_button = mock_press_mouse,
        .release_mouse_button = mock_release_mouse,
    };

    zmk_behavior_app_t app;
    zmk_behavior_construct(&app, EDGE_MOD_ZMK_BEHAVIOR, 50, &hid_if, NULL);
    assert_int_equal(zmk_behavior_init(&app), EDGE_OK);

    // 1. Key Toggle: Press 1 toggles ON, Press 2 toggles OFF
    assert_int_equal(zmk_behavior_invoke(&app, ZMK_BHV_KEY_TOGGLE, 0x14, 0, true, 100),
                     EDGE_OK); // Q (0x14)
    assert_int_equal(hid_ctx.press_calls, 1);
    assert_int_equal(hid_ctx.last_pressed_kc, 0x14);

    assert_int_equal(zmk_behavior_invoke(&app, ZMK_BHV_KEY_TOGGLE, 0x14, 0, true, 150),
                     EDGE_OK); // Toggle OFF
    assert_int_equal(hid_ctx.release_calls, 1);
    assert_int_equal(hid_ctx.last_released_kc, 0x14);

    // 2. Macro execution with WAIT action: Tap 'H' (0x0B), WAIT 50ms, Tap 'I' (0x0C)
    zmk_macro_config_t macro_cfg = {
        .step_count = 3,
        .default_wait_ms = 10,
        .steps =
            {
                {.action = ZMK_MACRO_ACTION_TAP, .keycode = 0x0B, .modifiers = 0, .wait_ms = 15},
                {.action = ZMK_MACRO_ACTION_WAIT, .keycode = 0, .modifiers = 0, .wait_ms = 50},
                {.action = ZMK_MACRO_ACTION_TAP, .keycode = 0x0C, .modifiers = 0, .wait_ms = 15},
            },
    };
    uint8_t macro_idx = 0;
    assert_int_equal(zmk_behavior_add_macro(&app, &macro_cfg, &macro_idx), EDGE_OK);

    int prev_press = hid_ctx.press_calls;
    int prev_release = hid_ctx.release_calls;
    assert_int_equal(zmk_behavior_invoke(&app, ZMK_BHV_MACRO, macro_idx, 0, true, 200), EDGE_OK);
    // Should have pressed & released 2 keys (4 total events) and advanced time: 200 + 15 + 50 + 15
    // = 280
    assert_int_equal(hid_ctx.press_calls, prev_press + 2);
    assert_int_equal(hid_ctx.release_calls, prev_release + 2);
    assert_int_equal(hid_ctx.last_released_kc, 0x0C);
    assert_int_equal(app.current_time_ms, 280u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_behavior_key_press_and_repeat),
        cmocka_unit_test(test_behavior_hold_tap),
        cmocka_unit_test(test_behavior_hold_tap_flavors_and_retro),
        cmocka_unit_test(test_behavior_tap_dance),
        cmocka_unit_test(test_behavior_sticky_key_and_mod_morph),
        cmocka_unit_test(test_behavior_caps_word),
        cmocka_unit_test(test_behavior_macro_and_key_toggle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
