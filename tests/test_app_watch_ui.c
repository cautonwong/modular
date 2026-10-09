#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/events.h"
#include "edge/modules.h"
#include "watch_ui/watch_ui.h"

static uint8_t g_brightness_level = 0;
static uint16_t g_last_clear_color = 0;
static bool g_display_power = false;
static uint32_t g_clear_count = 0;
static uint32_t g_power_mode_count = 0;

static edge_status_t mock_set_brightness(void *self, uint8_t level) {
    (void)self;
    g_brightness_level = level;
    return EDGE_OK;
}

static edge_status_t mock_clear_screen(void *self, uint16_t color) {
    (void)self;
    g_clear_count++;
    g_last_clear_color = color;
    return EDGE_OK;
}

static edge_status_t mock_set_power_mode(void *self, bool display_on) {
    (void)self;
    g_power_mode_count++;
    g_display_power = display_on;
    return EDGE_OK;
}

static const watch_ui_display_port_t g_mock_display = {
    .self = NULL,
    .set_brightness = mock_set_brightness,
    .clear_screen = mock_clear_screen,
    .draw_bitmap = NULL,
    .set_power_mode = mock_set_power_mode,
};

static void reset_mocks(void) {
    g_brightness_level = 0;
    g_last_clear_color = 0;
    g_display_power = false;
    g_clear_count = 0;
    g_power_mode_count = 0;
}

static void test_watch_ui_init_and_getters(void **state) {
    (void)state;
    reset_mocks();

    watch_ui_t ui;
    assert_int_equal(watch_ui_init(&ui, &g_mock_display, NULL, NULL), EDGE_OK);

    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_WATCHFACE);
    assert_int_equal(watch_ui_get_watchface_style(&ui), WATCHFACE_STYLE_DIGITAL);

    watch_ui_set_watchface_style(&ui, WATCHFACE_STYLE_ANALOG);
    assert_int_equal(watch_ui_get_watchface_style(&ui), WATCHFACE_STYLE_ANALOG);

    assert_int_equal(ui.module.module_id, EDGE_MOD_WATCH_UI);
    assert_int_equal(ui.module.priority, 10u);
}

static void test_watch_ui_navigation_and_gestures(void **state) {
    (void)state;
    reset_mocks();

    watch_ui_t ui;
    watch_ui_construct(&ui, EDGE_MOD_WATCH_UI, 10u, &g_mock_display, NULL, NULL);

    /* Watchface: Swipe Up -> Notifications */
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_SWIPE_UP), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_NOTIFICATIONS);

    /* Button returns to Watchface */
    assert_int_equal(watch_ui_on_button_pressed(&ui), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_WATCHFACE);

    /* Watchface: Swipe Down -> Quick Settings */
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_SWIPE_DOWN), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_QUICK_SETTINGS);

    /* Swipe Right returns to Watchface */
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_SWIPE_RIGHT), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_WATCHFACE);

    /* Watchface: Swipe Left -> App Launcher */
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_SWIPE_LEFT), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_APP_LAUNCHER);
    assert_int_equal(ui.launcher_page, 0);

    /* Launcher page swipe left -> page 1 */
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_SWIPE_LEFT), EDGE_OK);
    assert_int_equal(ui.launcher_page, 1);

    /* Launcher page swipe right -> page 0 */
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_SWIPE_RIGHT), EDGE_OK);
    assert_int_equal(ui.launcher_page, 0);

    /* Launcher page 0 swipe right -> return to Watchface */
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_SWIPE_RIGHT), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_WATCHFACE);
}

static void test_watch_ui_events_and_screen_toggle(void **state) {
    (void)state;
    reset_mocks();

    watch_ui_t ui;
    watch_ui_construct(&ui, EDGE_MOD_WATCH_UI, 10u, &g_mock_display, NULL, NULL);

    /* Direct button press on watchface toggles screen state */
    assert_true(ui.screen_on);
    assert_int_equal(watch_ui_on_button_pressed(&ui), EDGE_OK);
    assert_false(ui.screen_on);
    assert_false(g_display_power);

    /* Button press again turns screen back on */
    assert_int_equal(watch_ui_on_button_pressed(&ui), EDGE_OK);
    assert_true(ui.screen_on);
    assert_true(g_display_power);

    /* Event dispatch: Touch event */
    edge_event_t touch_evt = {
        .id = EDGE_EVT_WATCH_TOUCH,
        .arg0 = WATCH_UI_GESTURE_SWIPE_UP,
    };
    assert_int_equal(ui.module.on_event(&ui.module, &touch_evt), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_NOTIFICATIONS);

    /* Button event returns to watchface */
    edge_event_t btn_evt = {
        .id = EDGE_EVT_WATCH_BUTTON,
        .arg0 = 0,
    };
    assert_int_equal(ui.module.on_event(&ui.module, &btn_evt), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_WATCHFACE);

    /* Wrist wake event */
    ui.screen_on = false;
    edge_event_t wrist_evt = {
        .id = EDGE_EVT_WATCH_WRIST_WAKE,
        .arg0 = 0,
    };
    assert_int_equal(ui.module.on_event(&ui.module, &wrist_evt), EDGE_OK);
    assert_true(ui.screen_on);
    assert_true(g_display_power);

    /* Flashlight screen clear color check */
    assert_int_equal(watch_ui_load_screen(&ui, WATCH_SCREEN_FLASHLIGHT), EDGE_OK);
    assert_int_equal(g_last_clear_color, 0xFFFF);

    /* Power off callback */
    assert_int_equal(ui.module.power_off(&ui.module), EDGE_OK);
    assert_false(ui.screen_on);
    assert_false(g_display_power);
}

static void test_watch_ui_all_styles_and_button_events(void **state) {
    (void)state;
    reset_mocks();

    watch_ui_t ui;
    watch_ui_construct(&ui, EDGE_MOD_WATCH_UI, 10u, &g_mock_display, NULL, NULL);

    /* Test all 7 watch face styles */
    watchface_style_t styles[] = {
        WATCHFACE_STYLE_DIGITAL, WATCHFACE_STYLE_ANALOG,   WATCHFACE_STYLE_PINETIME,
        WATCHFACE_STYLE_CASIO,   WATCHFACE_STYLE_TERMINAL, WATCHFACE_STYLE_INFINEAT,
        WATCHFACE_STYLE_PRIDE,
    };
    for (size_t i = 0; i < sizeof(styles) / sizeof(styles[0]); i++) {
        watch_ui_set_watchface_style(&ui, styles[i]);
        assert_int_equal(watch_ui_get_watchface_style(&ui), styles[i]);
    }

    /* Double tap gesture to sleep on watchface */
    assert_true(ui.screen_on);
    assert_int_equal(watch_ui_on_touch_gesture(&ui, WATCH_UI_GESTURE_DOUBLE_TAP), EDGE_OK);
    assert_false(ui.screen_on);
    assert_false(g_display_power);

    /* Wake screen */
    ui.screen_on = true;

    /* Double click button loads notification preview */
    assert_int_equal(watch_ui_on_button_event(&ui, WATCH_UI_BUTTON_DOUBLE_CLICK), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_NOTIF_PREVIEW);

    /* Long press button on any app screen returns to watchface and resets stack */
    assert_int_equal(watch_ui_on_button_event(&ui, WATCH_UI_BUTTON_LONG_PRESS), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_WATCHFACE);
    assert_int_equal(ui.stack_ptr, 0);

    /* Longer press button loads SysInfo / reboot screen */
    assert_int_equal(watch_ui_on_button_event(&ui, WATCH_UI_BUTTON_LONGER_PRESS), EDGE_OK);
    assert_int_equal(watch_ui_get_current_screen(&ui), WATCH_SCREEN_SYSINFO);

    /* Test loading all 40 screen IDs */
    for (int s = 0; s <= WATCH_SCREEN_SETTING_OTA; s++) {
        assert_int_equal(watch_ui_load_screen(&ui, (watch_screen_id_t)s), EDGE_OK);
        assert_int_equal(watch_ui_get_current_screen(&ui), (watch_screen_id_t)s);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_watch_ui_init_and_getters),
        cmocka_unit_test(test_watch_ui_navigation_and_gestures),
        cmocka_unit_test(test_watch_ui_events_and_screen_toggle),
        cmocka_unit_test(test_watch_ui_all_styles_and_button_events),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
