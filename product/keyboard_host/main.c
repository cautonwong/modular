#include "edge/clock.h"
#include "edge/event.h"
#include "edge/events.h"
#include "edge/modules.h"
#include "example/sys.h"
#include "glue.h"
#include "hid_report/hid_report.h"
#include "keyboard_mock/board.h"
#include "zmk_behavior/behavior.h"
#include "zmk_endpoints/endpoints.h"
#include "zmk_keymap/keymap.h"
#include "zmk_matrix/zmk_matrix.h"
#include "zmk_pm/pm.h"
#include "zmk_wpm/wpm.h"

#include <stdint.h>
#include <stdio.h>

static uint64_t monotonic_ticks(void *self) {
    return ++(*(uint64_t *)self);
}

int main(void) {
    uint64_t clock_tick = 0u;
    edge_clock_port_t clock = {
        .monotonic_ticks = monotonic_ticks, .wall_time = NULL, .self = &clock_tick};

    /* Infra components */
    hid_report_builder_t hid_builder;
    hid_report_builder_init(&hid_builder);

    /* Ports */
    zmk_behavior_hid_if_t behavior_hid;
    product_keyboard_make_behavior_hid(&behavior_hid, &hid_builder);

    /* Behavior app */
    zmk_behavior_app_t behavior_app;
    zmk_behavior_construct(&behavior_app, EDGE_MOD_ZMK_BEHAVIOR, 30u, &behavior_hid, NULL);
    if (zmk_behavior_init(&behavior_app) < 0) {
        return 10;
    }

    zmk_keymap_behavior_if_t keymap_behavior;
    product_keyboard_make_keymap_behavior(&keymap_behavior, &behavior_app);

    /* Keymap app */
    zmk_keymap_app_t keymap_app;
    zmk_keymap_construct(&keymap_app, EDGE_MOD_ZMK_KEYMAP, 40u, &keymap_behavior, NULL, 4, 32);
    if (zmk_keymap_init(&keymap_app) < 0) {
        return 11;
    }

    /* Set some sample default bindings: Pos 0 -> Key A (0x04), Pos 1 -> Key B (0x05) */
    zmk_keymap_set_binding(&keymap_app, 0, 0, (zmk_behavior_binding_t){ZMK_BHV_KEY_PRESS, 0x04, 0});
    zmk_keymap_set_binding(&keymap_app, 0, 1, (zmk_behavior_binding_t){ZMK_BHV_KEY_PRESS, 0x05, 0});

    zmk_matrix_event_sink_if_t matrix_sink;
    product_keyboard_make_matrix_sink(&matrix_sink, &keymap_app);

    /* Matrix app */
    zmk_matrix_app_t matrix_app;
    zmk_matrix_construct(&matrix_app, EDGE_MOD_ZMK_MATRIX, 50u, NULL, &matrix_sink, NULL);
    if (zmk_matrix_init(&matrix_app) < 0) {
        return 12;
    }

    /* Endpoints app */
    zmk_endpoints_app_t endpoints_app;
    zmk_endpoints_construct(&endpoints_app, EDGE_MOD_ZMK_ENDPOINTS, 60u, NULL);
    if (zmk_endpoints_init(&endpoints_app) < 0) {
        return 13;
    }

    /* PM app */
    zmk_pm_app_t pm_app;
    zmk_pm_construct(&pm_app, EDGE_MOD_ZMK_PM, 70u, NULL, NULL);
    if (zmk_pm_init(&pm_app) < 0) {
        return 14;
    }

    /* WPM app */
    zmk_wpm_app_t wpm_app;
    zmk_wpm_construct(&wpm_app, EDGE_MOD_ZMK_WPM, 80u, NULL);
    if (zmk_wpm_init(&wpm_app) < 0) {
        return 15;
    }

    /* Module array */
    edge_module_t *apps[6];
    apps[0] = &behavior_app.module;
    apps[1] = &keymap_app.module;
    apps[2] = &matrix_app.module;
    apps[3] = &endpoints_app.module;
    apps[4] = &pm_app.module;
    apps[5] = &wpm_app.module;

    edge_event_t event_storage[16];
    edge_event_queue_t event_queue;
    if (edge_event_queue_init(&event_queue, event_storage, 16u) < 0) {
        return 1;
    }
    edge_event_sink_t event_sink = {.queue = &event_queue, .clock = &clock, .guard = NULL};
    board_keyboard_mock_init(&event_sink);

    edge_sys_subscription_t subscriptions[8];
    edge_sys_t sys;
    if (sys_example_init(&sys, apps, 6u, &event_queue, subscriptions, 8u) < 0) {
        return 2;
    }
    if (edge_sys_set_clock(&sys, &clock) < 0) {
        return 3;
    }
    if (edge_sys_subscribe(&sys, EDGE_EVT_ZMK_POSITION_STATE_CHANGED, &matrix_app.module) < 0) {
        return 4;
    }
    if (edge_sys_start(&sys) < 0) {
        return 5;
    }

    /* Simulate matrix key press */
    zmk_matrix_on_key_state_change(&matrix_app, 0, 0, true, 100);
    zmk_wpm_record_keystroke(&wpm_app, 100);
    zmk_pm_notify_activity(&pm_app, 100);

    /* Run one sys cycle */
    if (edge_sys_run_once(&sys) < 0) {
        return 6;
    }

    /* Verify key press recorded in HID builder */
    if (!hid_report_is_keycode_pressed(&hid_builder, 0x04)) {
        return 7;
    }

    (void)edge_sys_power_off(&sys);
    (void)zmk_behavior_shutdown(&behavior_app);
    (void)zmk_keymap_shutdown(&keymap_app);
    (void)zmk_matrix_shutdown(&matrix_app);
    (void)zmk_endpoints_shutdown(&endpoints_app);
    (void)zmk_pm_shutdown(&pm_app);
    (void)zmk_wpm_shutdown(&wpm_app);
    (void)edge_sys_deinit(&sys);

    return 0;
}
