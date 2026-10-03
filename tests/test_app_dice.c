#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "dice/dice.h"
#include "edge/modules.h"

static uint32_t g_vibrate_count = 0;

static uint32_t mock_entropy(void *self) {
    (void)self;
    return 0x12345678u;
}

static edge_status_t mock_vibrate(void *self) {
    (void)self;
    g_vibrate_count++;
    return EDGE_OK;
}

static const dice_entropy_if_t g_entropy = {
    .self = NULL,
    .get_entropy_seed = mock_entropy,
};

static const dice_motor_if_t g_motor = {
    .self = NULL,
    .vibrate = mock_vibrate,
};

static void test_dice_init_and_roll(void **state) {
    (void)state;
    g_vibrate_count = 0;

    dice_app_t dice;
    assert_int_equal(dice_init(&dice, &g_entropy, &g_motor), EDGE_OK);
    assert_int_equal(dice.module.module_id, EDGE_MOD_DICE);

    assert_int_equal(dice_get_count(&dice), 1);
    assert_int_equal(dice_get_sides(&dice), DICE_D6);
    assert_true(dice_get_result(&dice, 0) >= 1 && dice_get_result(&dice, 0) <= 6);

    /* Roll 3 D20 dice */
    assert_int_equal(dice_set_count(&dice, 3), EDGE_OK);
    assert_int_equal(dice_set_sides(&dice, DICE_D20), EDGE_OK);

    assert_int_equal(dice_roll(&dice), EDGE_OK);
    assert_true(g_vibrate_count >= 1);

    uint32_t sum = 0;
    for (uint8_t i = 0; i < 3; ++i) {
        uint8_t res = dice_get_result(&dice, i);
        assert_true(res >= 1 && res <= 20);
        sum += res;
    }
    assert_int_equal(dice_get_total_sum(&dice), sum);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dice_init_and_roll),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
