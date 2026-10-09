#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "game_paddle/game_paddle.h"

typedef struct mock_paddle {
    uint16_t vibrate_count;
    uint32_t random_val;
} mock_paddle_t;

static edge_status_t mock_vibrate(void *self, uint16_t duration_ms) {
    (void)duration_ms;
    mock_paddle_t *m = (mock_paddle_t *)self;
    m->vibrate_count++;
    return EDGE_OK;
}

// cppcheck-suppress constParameterPointer ; signature fixed by consumer port
// cppcheck-suppress constParameterCallback ; signature fixed by consumer port
static uint32_t mock_random(void *self) {
    const mock_paddle_t *m = (const mock_paddle_t *)self;
    return m->random_val;
}

static void test_game_paddle_physics_and_score(void **state) {
    (void)state;
    mock_paddle_t mock = {.random_val = 1};
    paddle_motor_if_t motor = {.self = &mock, .vibrate = mock_vibrate};
    paddle_entropy_if_t entropy = {.self = &mock, .get_random = mock_random};

    game_paddle_app_t app;
    game_paddle_construct(&app, EDGE_MOD_GAME_PADDLE, 20u, &motor, &entropy);
    assert_int_equal(game_paddle_init(&app), EDGE_OK);

    assert_int_equal(game_paddle_get_score(&app), 0);

    /* Move paddle to middle */
    game_paddle_set_paddle_pos(&app, 120u);
    assert_int_equal(game_paddle_get_paddle_pos(&app), 120u);

    /* Simulate multiple ticks and verify ball moves */
    int16_t x0, y0, x1, y1;
    game_paddle_get_ball_pos(&app, &x0, &y0);
    game_paddle_tick(&app);
    game_paddle_get_ball_pos(&app, &x1, &y1);

    assert_true(x1 != x0 || y1 != y0);

    /* Force a ball collision directly in front of the paddle */
    app.ball_x = 2;
    app.ball_y = 120;
    app.dx = -2;
    app.dy = 0;
    game_paddle_tick(&app);

    /* Should hit paddle, invert dx to positive, and increase score */
    assert_true(app.dx > 0);
    assert_int_equal(game_paddle_get_score(&app), 1);
    assert_int_equal(game_paddle_get_high_score(&app), 1);

    /* Force ball behind paddle (miss) */
    app.ball_x = -35;
    app.dx = -2;
    game_paddle_tick(&app);

    /* Score resets to 0, vibrate called, ball reset to center */
    assert_int_equal(game_paddle_get_score(&app), 0);
    assert_int_equal(game_paddle_get_high_score(&app), 1);
    assert_int_equal(mock.vibrate_count, 1);
}

static void test_game_paddle_bounds_and_ceiling_floor_bounces(void **state) {
    (void)state;
    mock_paddle_t mock = {.random_val = 2};
    paddle_motor_if_t motor = {.self = &mock, .vibrate = mock_vibrate};
    paddle_entropy_if_t entropy = {.self = &mock, .get_random = mock_random};

    game_paddle_app_t app;
    game_paddle_construct(&app, EDGE_MOD_GAME_PADDLE, 20u, &motor, &entropy);
    game_paddle_init(&app);

    /* Test paddle clamping at top and bottom */
    game_paddle_set_paddle_pos(&app, 10u);
    assert_int_equal(game_paddle_get_paddle_pos(&app), 30u);

    game_paddle_set_paddle_pos(&app, 235u);
    assert_int_equal(game_paddle_get_paddle_pos(&app), PADDLE_SCREEN_HEIGHT - 31u);

    /* Test ceiling bounce (y <= 1) */
    app.ball_x = 100;
    app.ball_y = 1;
    app.dx = 2;
    app.dy = -3;
    game_paddle_tick(&app);
    assert_true(app.dy > 0);

    /* Test floor bounce */
    app.ball_x = 100;
    app.ball_y = PADDLE_SCREEN_HEIGHT - PADDLE_BALL_SIZE - 2;
    app.dx = 2;
    app.dy = 3;
    game_paddle_tick(&app);
    assert_true(app.dy < 0);

    /* Test right wall collision */
    app.ball_x = PADDLE_SCREEN_WIDTH - PADDLE_BALL_SIZE - 1;
    app.ball_y = 100;
    app.dx = 2;
    app.dy = 0;
    game_paddle_tick(&app);
    assert_true(app.dx < 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_game_paddle_physics_and_score),
        cmocka_unit_test(test_game_paddle_bounds_and_ceiling_floor_bounces),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
