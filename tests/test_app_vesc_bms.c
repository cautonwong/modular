#include "vesc_bms/vesc_bms.h"
#include <cmocka.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static void test_vesc_bms_lifecycle(void **state) {
    (void)state;
    vesc_bms_app_t bms;
    vesc_bms_construct(&bms, 0x2E00, 50u, NULL, NULL);

    assert_int_equal(bms.module.module_id, 0x2E00);
    assert_int_equal(bms.module.priority, 50u);
    assert_ptr_equal(vesc_bms_module(&bms), &bms.module);
    assert_int_equal(vesc_bms_init(&bms), EDGE_OK);
}

static void test_vesc_bms_can_processing_and_limits(void **state) {
    (void)state;
    vesc_bms_app_t bms;
    bms_config_t cfg = {
        .cell_count = 10u,
        .v_cell_min = 3.0f,
        .v_cell_max = 4.2f,
        .temp_max_c = 60.0f,
        .i_in_max_a = 40.0f,
        .i_out_max_a = 60.0f,
        .soc_limit_start = 0.90f,
        .soc_limit_end = 1.0f,
    };
    vesc_bms_construct(&bms, 0x2E00, 50u, &cfg, NULL);
    assert_int_equal(vesc_bms_init(&bms), EDGE_OK);

    /* 1. Receive V_TOT frame: 41.5V (415), current 15.0A (150) */
    uint8_t frame_vtot[4] = {0x01, 0x9F, 0x00, 0x96};
    assert_int_equal(vesc_bms_process_can_frame(&bms, 0x30, frame_vtot, 4), EDGE_OK);

    bms_values_t vals;
    vesc_bms_get_values(&bms, &vals);
    assert_true(fabsf(vals.v_tot - 41.5f) < 0.05f);
    assert_true(fabsf(vals.i_in - 15.0f) < 0.05f);

    /* 2. Poll module -> calculates SOC: (4.15 - 3.0) / (4.2 - 3.0) = 1.15 / 1.2 = ~0.958 */
    assert_int_equal(bms.module.poll(&bms.module), EDGE_OK);
    vesc_bms_get_values(&bms, &vals);
    assert_true(vals.soc > 0.90f);

    /* 3. Check de-rated limits */
    float i_min = 0.0f, i_max = 0.0f;
    vesc_bms_update_limits(&bms, &i_min, &i_max);
    assert_true(i_min > -40.0f); /* charging current throttled */
    assert_true(i_max == 60.0f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_bms_lifecycle),
        cmocka_unit_test(test_vesc_bms_can_processing_and_limits),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
