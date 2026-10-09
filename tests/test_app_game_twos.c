#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/modules.h"
#include "game_twos/game_twos.h"

typedef struct mock_twos_ctx {
    uint32_t counter;
} mock_twos_ctx_t;

static uint32_t mock_random(void *self) {
    mock_twos_ctx_t *ctx = (mock_twos_ctx_t *)self;
    ctx->counter++;
    return ctx->counter * 17u;
}

static void test_game_twos_init_and_merge(void **state) {
    (void)state;
    mock_twos_ctx_t mock = {0};
    twos_entropy_if_t entropy = {.self = &mock, .get_random = mock_random};

    game_twos_app_t app;
    game_twos_construct(&app, EDGE_MOD_GAME_TWOS, 20u, &entropy);
    assert_int_equal(game_twos_init(&app), EDGE_OK);

    /* Initially two tiles placed */
    int count = 0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (game_twos_get_cell(&app, (uint8_t)r, (uint8_t)c) > 0) {
                count++;
            }
        }
    }
    assert_int_equal(count, 2);

    /* Set known board state */
    app.grid[0][0].value = 2;
    app.grid[0][1].value = 2;
    app.grid[0][2].value = 4;
    app.grid[0][3].value = 4;
    for (int r = 1; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            app.grid[r][c].value = 0;
        }
    }

    /* Swipe Left -> [4, 8, 0, 0] + 1 random spawn */
    bool moved = game_twos_swipe(&app, TWOS_DIR_LEFT);
    assert_true(moved);
    assert_int_equal(app.grid[0][0].value, 4);
    assert_int_equal(app.grid[0][1].value, 8);
    assert_int_equal(game_twos_get_score(&app), 12); /* 4 + 8 */
    assert_int_equal(game_twos_get_high_score(&app), 12);
}

static void test_game_twos_swipes_and_game_over(void **state) {
    (void)state;
    mock_twos_ctx_t mock = {0};
    twos_entropy_if_t entropy = {.self = &mock, .get_random = mock_random};

    game_twos_app_t app;
    game_twos_construct(&app, EDGE_MOD_GAME_TWOS, 20u, &entropy);
    game_twos_init(&app);

    /* Set vertical merge board state: [2], [2], [4], [4] in col 0 */
    app.grid[0][0].value = 2;
    app.grid[1][0].value = 2;
    app.grid[2][0].value = 4;
    app.grid[3][0].value = 4;
    for (int r = 0; r < 4; r++) {
        for (int c = 1; c < 4; c++) {
            app.grid[r][c].value = 0;
        }
    }

    /* Swipe UP -> col 0 becomes [4, 8, 0, 0] */
    bool moved = game_twos_swipe(&app, TWOS_DIR_UP);
    assert_true(moved);
    assert_int_equal(app.grid[0][0].value, 4);
    assert_int_equal(app.grid[1][0].value, 8);
    assert_int_equal(game_twos_get_score(&app), 12);

    /* Test full board with alternating values -> Game Over */
    uint16_t alt[4][4] = {
        {2, 4, 2, 4},
        {4, 2, 4, 2},
        {2, 4, 2, 4},
        {4, 2, 4, 2},
    };
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            app.grid[r][c].value = alt[r][c];
        }
    }

    /* Swipe in any direction -> cannot move */
    assert_false(game_twos_swipe(&app, TWOS_DIR_LEFT));
    assert_false(game_twos_swipe(&app, TWOS_DIR_RIGHT));
    assert_false(game_twos_swipe(&app, TWOS_DIR_UP));
    assert_false(game_twos_swipe(&app, TWOS_DIR_DOWN));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_game_twos_init_and_merge),
        cmocka_unit_test(test_game_twos_swipes_and_game_over),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
