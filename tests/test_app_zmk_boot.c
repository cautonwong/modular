/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#include "contract/app_contract.h"
#include "edge/modules.h"
#include "zmk_boot/boot.h"

typedef struct mock_boot_hw {
    int calls;
    zmk_boot_target_t last_target;
} mock_boot_hw_t;

static edge_status_t mock_reboot_to_target(void *self, zmk_boot_target_t target) {
    mock_boot_hw_t *h = (mock_boot_hw_t *)self;
    h->calls++;
    h->last_target = target;
    return EDGE_OK;
}

static void test_boot_double_tap_trigger(void **state) {
    (void)state;
    mock_boot_hw_t hw_ctx = {0};
    zmk_boot_hw_if_t hw = {.self = &hw_ctx, .reboot_to_target = mock_reboot_to_target};

    zmk_boot_app_t app;
    zmk_boot_construct(&app, EDGE_MOD_ZMK_BOOT, 90, &hw);
    assert_int_equal(zmk_boot_init(&app), EDGE_OK);

    // Single tap at t=100ms -> no reboot yet
    assert_int_equal(zmk_boot_reset_pressed(&app, 100), EDGE_OK);
    assert_int_equal(hw_ctx.calls, 0);

    // Second tap at t=350ms (within 500ms window) -> bootloader jump triggered!
    assert_int_equal(zmk_boot_reset_pressed(&app, 350), EDGE_OK);
    assert_int_equal(hw_ctx.calls, 1);
    assert_int_equal(hw_ctx.last_target, ZMK_BOOT_TARGET_BOOTLOADER);

    // Slow taps (separated by >500ms) -> no trigger
    assert_int_equal(zmk_boot_reset_pressed(&app, 1000), EDGE_OK);
    assert_int_equal(hw_ctx.calls, 1);
    assert_int_equal(zmk_boot_reset_pressed(&app, 1600), EDGE_OK);
    assert_int_equal(hw_ctx.calls, 1); // No new jump
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_boot_double_tap_trigger),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
