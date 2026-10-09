#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "flashlight/flashlight.h"

typedef struct mock_state {
    uint8_t brightness;
    bool screen_white;
    bool wake_locked;
} mock_state_t;

static edge_status_t mock_set_brightness(void *self, uint8_t percent) {
    mock_state_t *s = (mock_state_t *)self;
    s->brightness = percent;
    return EDGE_OK;
}

static edge_status_t mock_set_screen_color(void *self, bool is_white) {
    mock_state_t *s = (mock_state_t *)self;
    s->screen_white = is_white;
    return EDGE_OK;
}

static edge_status_t mock_set_wake_lock(void *self, bool locked) {
    mock_state_t *s = (mock_state_t *)self;
    s->wake_locked = locked;
    return EDGE_OK;
}

static void test_flashlight_toggle_and_levels(void **state) {
    (void)state;
    mock_state_t mock = {0};
    flashlight_display_if_t disp = {
        .self = &mock,
        .set_brightness = mock_set_brightness,
        .set_screen_color = mock_set_screen_color,
    };
    flashlight_system_if_t sys = {
        .self = &mock,
        .set_wake_lock = mock_set_wake_lock,
    };

    flashlight_app_t app;
    flashlight_construct(&app, EDGE_MOD_FLASHLIGHT, 20u, &disp, &sys);
    assert_int_equal(flashlight_init(&app), EDGE_OK);

    flashlight_open(&app, 80u);
    assert_true(mock.wake_locked);
    assert_false(flashlight_is_on(&app));
    assert_false(mock.screen_white);

    /* Toggle ON */
    flashlight_toggle(&app);
    assert_true(flashlight_is_on(&app));
    assert_true(mock.screen_white);
    assert_int_equal(mock.brightness, 100u); /* High level */

    /* Swipe Left -> Medium */
    flashlight_swipe_left(&app);
    assert_int_equal(flashlight_get_level(&app), FLASHLIGHT_LEVEL_MEDIUM);
    assert_int_equal(mock.brightness, 66u);

    /* Swipe Left -> Low */
    flashlight_swipe_left(&app);
    assert_int_equal(flashlight_get_level(&app), FLASHLIGHT_LEVEL_LOW);
    assert_int_equal(mock.brightness, 33u);

    /* Swipe Right -> Medium */
    flashlight_swipe_right(&app);
    assert_int_equal(flashlight_get_level(&app), FLASHLIGHT_LEVEL_MEDIUM);
    assert_int_equal(mock.brightness, 66u);

    /* Close -> restores brightness and unlocks wake */
    flashlight_close(&app);
    assert_false(flashlight_is_on(&app));
    assert_false(mock.screen_white);
    assert_int_equal(mock.brightness, 80u);
    assert_false(mock.wake_locked);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_flashlight_toggle_and_levels),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
