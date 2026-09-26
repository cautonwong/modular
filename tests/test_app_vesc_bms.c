/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "vesc_bms/vesc_bms.h"

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

/*
 * The BMS paths the lifecycle and limit tests do not reach: the SOC clamps at both ends, the
 * three fault bits with their two guards (a cell near zero is below the 0.1 liveness floor and is
 * ignored, and a zero temperature limit disables that check), the guards that skip the SOC
 * calculation entirely, the defaults a NULL configuration is repaired with, and module(NULL).
 */
static void test_vesc_bms_edges(void **state) {
    (void)state;
    vesc_bms_app_t bms;

    vesc_bms_construct(&bms, 0x2E00, 50u, NULL, NULL);
    assert_int_equal(vesc_bms_init(&bms), EDGE_OK);
    assert_int_equal(bms.config.cell_count, 12u);
    assert_true(bms.config.v_cell_max > bms.config.v_cell_min);
    assert_float_equal(bms.config.temp_max_c, 65.0f, 1e-6f);

    /* SOC clamps: below the empty cell voltage it floors at zero, above full it caps at one. */
    bms.values.v_tot = 10.0f; /* 0.83 V per cell */
    assert_int_equal(bms.module.poll(&bms.module), EDGE_OK);
    assert_float_equal(bms.values.soc, 0.0f, 1e-6f);

    bms.values.v_tot = 60.0f; /* 5 V per cell */
    assert_int_equal(bms.module.poll(&bms.module), EDGE_OK);
    assert_float_equal(bms.values.soc, 1.0f, 1e-6f);

    /* The three fault bits, plus a cell that must not raise any because it is not live. */
    memset(&bms.values, 0, sizeof(bms.values));
    bms.values.cell_voltages[0] = 4.5f;  /* above v_cell_max + 0.1 */
    bms.values.cell_voltages[1] = 2.5f;  /* below v_cell_min - 0.2 */
    bms.values.cell_voltages[2] = 0.05f; /* below the 0.1 liveness floor */
    bms.values.temp_sensors[0] = 70.0f;  /* above 65 */
    assert_int_equal(bms.module.poll(&bms.module), EDGE_OK);
    assert_int_equal(bms.fault_code & (1u << 0), 1u << 0);
    assert_int_equal(bms.fault_code & (1u << 1), 1u << 1);
    assert_int_equal(bms.fault_code & (1u << 2), 1u << 2);

    /* A dead cell alone is not a fault, and neither is a limit that is switched off. */
    memset(&bms.values, 0, sizeof(bms.values));
    bms.values.cell_voltages[0] = 0.05f;
    bms.values.temp_sensors[0] = 100.0f;
    bms.config.temp_max_c = 0.0f;
    assert_int_equal(bms.module.poll(&bms.module), EDGE_OK);
    assert_int_equal(bms.fault_code, 0u);

    /* With no cells configured there is nothing to compute, so the SOC stays as it was. */
    bms.config.cell_count = 0u;
    bms.values.soc = 0.42f;
    bms.values.v_tot = 48.0f;
    assert_int_equal(bms.module.poll(&bms.module), EDGE_OK);
    assert_float_equal(bms.values.soc, 0.42f, 1e-6f);

    /* ... and the same when the cell range is degenerate. */
    bms.config.cell_count = 12u;
    bms.config.v_cell_max = bms.config.v_cell_min;
    assert_int_equal(bms.module.poll(&bms.module), EDGE_OK);
    assert_float_equal(bms.values.soc, 0.42f, 1e-6f);

    assert_ptr_equal(vesc_bms_module(NULL), NULL);
}

/*
 * The two CAN frames the first case does not send, and the limit paths. The cell frame carries a
 * start index and two bytes per cell, the temperature frame one byte per sensor signed - so a
 * negative reading has to arrive negative rather than as a large positive one - and the limits are
 * zeroed on a fault and derated as the state of charge approaches full.
 */
static void test_vesc_bms_cell_and_temp_frames_and_limits(void **state) {
    (void)state;
    vesc_bms_app_t bms;
    bms_config_t cfg = {.cell_count = 10u,
                        .v_cell_min = 3.0f,
                        .v_cell_max = 4.2f,
                        .temp_max_c = 60.0f,
                        .i_in_max_a = 40.0f,
                        .i_out_max_a = 60.0f,
                        .soc_limit_start = 0.90f,
                        .soc_limit_end = 1.0f};
    vesc_bms_construct(&bms, 0x2E00, 50u, &cfg, NULL);
    assert_int_equal(vesc_bms_init(&bms), EDGE_OK);

    /* Guards on the frame handler. */
    assert_int_equal(vesc_bms_process_can_frame(NULL, 0x30, (const uint8_t *)"abcd", 4u),
                     EDGE_EINVAL);
    assert_int_equal(vesc_bms_process_can_frame(&bms, 0x30, NULL, 4u), EDGE_EINVAL);
    assert_int_equal(vesc_bms_process_can_frame(&bms, 0x30, (const uint8_t *)"ab", 2u),
                     EDGE_EINVAL);

    /* Cell voltages: start index 0, then 4000 mV and 3900 mV. */
    const uint8_t cells[5] = {0u, 0x0Fu, 0xA0u, 0x0Fu, 0x3Cu}; /* 4000 mV, 3900 mV */
    assert_int_equal(vesc_bms_process_can_frame(&bms, 0x31, cells, sizeof(cells)), EDGE_OK);
    bms_values_t vals;
    vesc_bms_get_values(&bms, &vals);
    assert_float_equal(vals.cell_voltages[0], 4.0f, 1e-4f);
    assert_float_equal(vals.cell_voltages[1], 3.9f, 1e-4f);

    /* A start index that would run past the array is skipped rather than written past it. */
    const uint8_t far_cells[4] = {200u, 0x0Fu, 0xA0u, 0x0Fu};
    assert_int_equal(vesc_bms_process_can_frame(&bms, 0x31, far_cells, sizeof(far_cells)), EDGE_OK);

    /* Temperatures: start index 0, then +25 C and -10 C. */
    const uint8_t temps[4] = {0u, 25u, (uint8_t)(int8_t)-10, 30u};
    assert_int_equal(vesc_bms_process_can_frame(&bms, 0x32, temps, sizeof(temps)), EDGE_OK);
    vesc_bms_get_values(&bms, &vals);
    assert_float_equal(vals.temp_sensors[0], 25.0f, 1e-4f);
    assert_float_equal(vals.temp_sensors[1], -10.0f, 1e-4f);

    /* A fault zeroes both limits rather than leaving a stale window. */
    bms.fault_code = 1u;
    float i_min = 1.0f;
    float i_max = 1.0f;
    vesc_bms_update_limits(&bms, &i_min, &i_max);
    assert_float_equal(i_min, 0.0f, 1e-6f);
    assert_float_equal(i_max, 0.0f, 1e-6f);

    /* With no fault, the derating falls on the charging side: the configuration's input limit is
     * scaled down as the state of charge approaches full, and it is reported as the negative
     * current the pack will accept. The discharge side is the pack's own. */
    bms.fault_code = 0u;
    bms.values.soc = 0.0f;
    vesc_bms_update_limits(&bms, &i_min, &i_max);
    const float charge_empty = i_min;
    const float discharge_empty = i_max;
    bms.values.soc = 0.95f;
    vesc_bms_update_limits(&bms, &i_min, &i_max);
    assert_true(i_min > charge_empty); /* less negative: charging is being cut back */
    assert_float_equal(i_max, discharge_empty, 1e-6f);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_bms_lifecycle),
        cmocka_unit_test(test_vesc_bms_can_processing_and_limits),
        cmocka_unit_test(test_vesc_bms_edges),
        cmocka_unit_test(test_vesc_bms_cell_and_temp_frames_and_limits),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
