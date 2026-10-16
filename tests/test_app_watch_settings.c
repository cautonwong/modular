/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "edge/modules.h"
#include "watch_settings/watch_settings.h"

static watch_settings_data_t g_mock_store_data;
static uint32_t g_store_load_count = 0;
static uint32_t g_store_save_count = 0;

static edge_status_t mock_store_load(void *self, watch_settings_data_t *out_data) {
    (void)self;
    g_store_load_count++;
    *out_data = g_mock_store_data;
    return EDGE_OK;
}

static edge_status_t mock_store_save(void *self, const watch_settings_data_t *data) {
    (void)self;
    g_store_save_count++;
    g_mock_store_data = *data;
    return EDGE_OK;
}

static void test_watch_settings_load_save_and_properties(void **state) {
    (void)state;

    g_store_load_count = 0;
    g_store_save_count = 0;
    g_mock_store_data = (watch_settings_data_t){
        .version = SETTINGS_FORMAT_VERSION,
        .steps_goal = 8000u,
        .screen_timeout_ms = 10000u,
        .always_on_display = false,
        .clock_format = CLOCK_FORMAT_12H,
        .weather_format = WEATHER_FORMAT_IMPERIAL,
        .notification_mode = NOTIF_ON,
        .watch_face = WATCH_FACE_PINETIME_STYLE,
        .chimes_mode = CHIMES_HOURS,
        .pts =
            {
                .color_time = 1u,
                .color_bar = 2u,
                .color_bg = 3u,
                .gauge_style = PTS_GAUGE_HALF,
                .weather_enable = false,
            },
        .pride_flag = PRIDE_FLAG_TRANS,
        .infineat =
            {
                .show_side_cover = false,
                .color_index = 4,
            },
        .wake_mode_flags = WAKE_MODE_FLAG_RAISE_WRIST,
        .shake_wake_threshold = 200u,
        .brightness_level = 3u,
        .heart_rate_background_period_s = 600u,
    };

    const watch_settings_store_if_t store_if = {
        .load = mock_store_load,
        .save = mock_store_save,
        .self = NULL,
    };

    watch_settings_app_t app;
    watch_settings_construct(&app, EDGE_MOD_WATCH_SETTINGS, 50u, &store_if);
    assert_int_equal(watch_settings_init(&app), EDGE_OK);
    assert_int_equal(g_store_load_count, 1);

    /* Verify loaded values */
    assert_int_equal(watch_settings_get_steps_goal(&app), 8000u);
    assert_int_equal(watch_settings_get_clock_format(&app), CLOCK_FORMAT_12H);
    assert_int_equal(watch_settings_get_weather_format(&app), WEATHER_FORMAT_IMPERIAL);
    assert_int_equal(watch_settings_get_watch_face(&app), WATCH_FACE_PINETIME_STYLE);
    assert_int_equal(watch_settings_get_chimes_mode(&app), CHIMES_HOURS);
    assert_int_equal(watch_settings_get_pride_flag(&app), PRIDE_FLAG_TRANS);
    assert_int_equal(watch_settings_get_shake_wake_threshold(&app), 200u);

    pts_settings_t loaded_pts = watch_settings_get_pts_settings(&app);
    assert_int_equal(loaded_pts.color_time, 1u);
    assert_int_equal(loaded_pts.color_bar, 2u);
    assert_int_equal(loaded_pts.color_bg, 3u);
    assert_int_equal(loaded_pts.gauge_style, PTS_GAUGE_HALF);
    assert_false(loaded_pts.weather_enable);

    infineat_settings_t loaded_infineat = watch_settings_get_infineat_settings(&app);
    assert_false(loaded_infineat.show_side_cover);
    assert_int_equal(loaded_infineat.color_index, 4);

    assert_true(watch_settings_is_wake_mode_enabled(&app, WAKE_MODE_FLAG_RAISE_WRIST));
    assert_false(watch_settings_is_wake_mode_enabled(&app, WAKE_MODE_FLAG_DOUBLE_TAP));

    /* Modify properties */
    watch_settings_set_steps_goal(&app, 12000u);
    watch_settings_set_clock_format(&app, CLOCK_FORMAT_24H);
    watch_settings_set_weather_format(&app, WEATHER_FORMAT_METRIC);
    watch_settings_set_chimes_mode(&app, CHIMES_HALF_HOURS);
    watch_settings_set_pride_flag(&app, PRIDE_FLAG_LESBIAN);
    watch_settings_set_shake_wake_threshold(&app, 180u);

    pts_settings_t new_pts = {
        .color_time = 5u,
        .color_bar = 6u,
        .color_bg = 7u,
        .gauge_style = PTS_GAUGE_NUMERIC,
        .weather_enable = true,
    };
    watch_settings_set_pts_settings(&app, &new_pts);

    infineat_settings_t new_infineat = {
        .show_side_cover = true,
        .color_index = 8,
    };
    watch_settings_set_infineat_settings(&app, &new_infineat);

    watch_settings_set_wake_mode(&app, WAKE_MODE_FLAG_SINGLE_TAP, true);
    assert_true(watch_settings_is_wake_mode_enabled(&app, WAKE_MODE_FLAG_SINGLE_TAP));

    /* Single tap disables double tap */
    watch_settings_set_wake_mode(&app, WAKE_MODE_FLAG_DOUBLE_TAP, true);
    assert_true(watch_settings_is_wake_mode_enabled(&app, WAKE_MODE_FLAG_DOUBLE_TAP));
    assert_false(watch_settings_is_wake_mode_enabled(&app, WAKE_MODE_FLAG_SINGLE_TAP));

    /* Save to store */
    assert_int_equal(watch_settings_save(&app), EDGE_OK);
    assert_int_equal(g_store_save_count, 1);
    assert_int_equal(g_mock_store_data.steps_goal, 12000u);
    assert_int_equal(g_mock_store_data.clock_format, CLOCK_FORMAT_24H);
    assert_int_equal(g_mock_store_data.weather_format, WEATHER_FORMAT_METRIC);
    assert_int_equal(g_mock_store_data.chimes_mode, CHIMES_HALF_HOURS);
    assert_int_equal(g_mock_store_data.pride_flag, PRIDE_FLAG_LESBIAN);
    assert_int_equal(g_mock_store_data.pts.gauge_style, PTS_GAUGE_NUMERIC);
    assert_true(g_mock_store_data.pts.weather_enable);
    assert_true(g_mock_store_data.infineat.show_side_cover);
    assert_int_equal(g_mock_store_data.infineat.color_index, 8);
    assert_int_equal(g_mock_store_data.shake_wake_threshold, 180u);

    watch_settings_data_t snapshot;
    assert_int_equal(watch_settings_get_data(&app, &snapshot), EDGE_OK);
    assert_int_equal(snapshot.steps_goal, 12000u);
}

static void test_watch_settings_invariants(void **state) {
    (void)state;

    watch_settings_app_t app;
    watch_settings_construct(&app, EDGE_MOD_WATCH_SETTINGS, 50u, NULL);
    assert_int_equal(watch_settings_init(&app), EDGE_OK);

    /* Initial defaults */
    assert_int_equal(watch_settings_get_brightness(&app), 2u);
    assert_int_equal(watch_settings_get_screen_timeout(&app), 15000u);
    assert_int_equal(watch_settings_get_watch_face(&app), WATCH_FACE_DIGITAL);
    assert_int_equal(watch_settings_get_steps_goal(&app), 10000u);

    /* Invalid brightness (out of range [1, 3]) must be rejected */
    watch_settings_set_brightness(&app, 0u);
    assert_int_equal(watch_settings_get_brightness(&app), 2u);
    watch_settings_set_brightness(&app, 10u);
    assert_int_equal(watch_settings_get_brightness(&app), 2u);

    /* Valid brightness accepted */
    watch_settings_set_brightness(&app, 3u);
    assert_int_equal(watch_settings_get_brightness(&app), 3u);

    /* Invalid screen timeout (<1000 or >300000) rejected */
    watch_settings_set_screen_timeout(&app, 100u);
    assert_int_equal(watch_settings_get_screen_timeout(&app), 15000u);
    watch_settings_set_screen_timeout(&app, 500000u);
    assert_int_equal(watch_settings_get_screen_timeout(&app), 15000u);

    /* Invalid steps goal (0 or >1000000) rejected */
    watch_settings_set_steps_goal(&app, 0u);
    assert_int_equal(watch_settings_get_steps_goal(&app), 10000u);
    watch_settings_set_steps_goal(&app, 2000000u);
    assert_int_equal(watch_settings_get_steps_goal(&app), 10000u);

    /* Invalid watchface enum rejected */
    watch_settings_set_watch_face(&app, (watch_face_type_t)99);
    assert_int_equal(watch_settings_get_watch_face(&app), WATCH_FACE_DIGITAL);

    /* Loading corrupted data from store is rejected */
    g_mock_store_data = (watch_settings_data_t){
        .version = SETTINGS_FORMAT_VERSION,
        .brightness_level = 200u, /* Invalid! */
        .steps_goal = 5000u,
        .screen_timeout_ms = 10000u,
    };
    const watch_settings_store_if_t store_if = {
        .load = mock_store_load,
        .save = mock_store_save,
        .self = NULL,
    };
    watch_settings_app_t corrupted_app;
    watch_settings_construct(&corrupted_app, EDGE_MOD_WATCH_SETTINGS, 50u, &store_if);
    assert_int_equal(watch_settings_init(&corrupted_app), EDGE_OK);
    /* Should retain default 10000u because corrupted loaded data was rejected */
    assert_int_equal(watch_settings_get_steps_goal(&corrupted_app), 10000u);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_watch_settings_load_save_and_properties),
        cmocka_unit_test(test_watch_settings_invariants),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
