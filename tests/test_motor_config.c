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
#include "edge/modules.h"
#include "motor_config/motor_config.h"

typedef struct mock_storage_ctx {
    uint8_t flash_mem[1024];
    size_t read_count;
    size_t write_count;
    size_t erase_count;
} mock_storage_ctx_t;

static edge_status_t mock_flash_read(void *self, uint32_t offset, uint8_t *buf, size_t len) {
    mock_storage_ctx_t *ctx = (mock_storage_ctx_t *)self;
    if (offset + len > sizeof(ctx->flash_mem)) {
        return EDGE_EINVAL;
    }
    memcpy(buf, ctx->flash_mem + offset, len);
    ctx->read_count++;
    return EDGE_OK;
}

static edge_status_t mock_flash_write(void *self, uint32_t offset, const uint8_t *buf, size_t len) {
    mock_storage_ctx_t *ctx = (mock_storage_ctx_t *)self;
    if (offset + len > sizeof(ctx->flash_mem)) {
        return EDGE_EINVAL;
    }
    memcpy(ctx->flash_mem + offset, buf, len);
    ctx->write_count++;
    return EDGE_OK;
}

static edge_status_t mock_flash_erase(void *self, uint32_t offset, size_t len) {
    mock_storage_ctx_t *ctx = (mock_storage_ctx_t *)self;
    if (offset + len > sizeof(ctx->flash_mem)) {
        return EDGE_EINVAL;
    }
    memset(ctx->flash_mem + offset, 0xFF, len);
    ctx->erase_count++;
    return EDGE_OK;
}

static void test_defaults_and_validation(void **state) {
    (void)state;
    mc_configuration_t mc;
    app_configuration_t app;

    motor_config_set_defaults(&mc, &app);
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_OK);

    assert_int_equal(mc.motor_type, MC_MOTOR_TYPE_FOC);
    assert_true(mc.current_max > 0.0f);
    assert_true(mc.current_min < 0.0f);
    assert_true(mc.v_in_max > mc.v_in_min);

    /* Test invalid configs */
    mc.current_max = -10.0f;
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_EINVAL);

    mc.current_max = 50.0f;
    mc.v_in_max = 5.0f; /* Less than v_in_min */
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_EINVAL);
}

/*
 * Defaults of the fields foc_core consumes are the reference's
 * (motor/mcconf_default.h). They were absent from the configuration entirely, so
 * the controller ran on compile-time constants that no configuration could change.
 */
static void test_controller_fields_have_reference_defaults(void **state) {
    (void)state;
    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);

    assert_float_equal(mc.foc_current_filter_const, 0.1f, 1e-6f);
    assert_float_equal(mc.foc_pll_kp, 2000.0f, 1e-6f);
    assert_float_equal(mc.foc_pll_ki, 30000.0f, 1e-6f);
    assert_float_equal(mc.l_max_duty, 0.95f, 1e-6f);
    assert_int_equal(mc.foc_observer_type, 0u); /* FOC_OBSERVER_ORTEGA_ORIGINAL */
    /* Compensation defaults: disabled, factor 0, no saliency difference. */
    assert_int_equal(mc.foc_sat_comp_mode, 0u); /* SAT_COMP_DISABLED */
    assert_float_equal(mc.foc_sat_comp, 0.0f, 1e-6f);
    assert_float_equal(mc.foc_motor_ld_lq_diff, 0.0f, 1e-6f);
    /* cc_min_current has a global reference default; the lo_* limits do not - they
     * are board-calibrated, and a 0 here means "pending the board". */
    assert_float_equal(mc.cc_min_current, 0.05f, 1e-6f);
    assert_float_equal(mc.l_abs_current_max, 0.0f, 1e-6f);
    assert_float_equal(mc.lo_current_min, 0.0f, 1e-6f);

    /* And the bounds the serialiser enforces on them. */
    mc.l_max_duty = 1.5f;
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_EINVAL);
    mc.l_max_duty = 0.9f;

    mc.foc_current_filter_const = 1.5f;
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_EINVAL);
    mc.foc_current_filter_const = 0.1f;

    mc.foc_observer_type = 7u;
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_EINVAL);
    mc.foc_observer_type = 4u; /* FOC_OBSERVER_MXV */
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_OK);
}

static void test_controller_fields_survive_a_round_trip(void **state) {
    (void)state;
    mc_configuration_t mc_orig, mc_restored;
    app_configuration_t app_orig, app_restored;

    motor_config_set_defaults(&mc_orig, &app_orig);
    mc_orig.foc_current_filter_const = 0.25f;
    mc_orig.foc_pll_kp = 1500.0f;
    mc_orig.foc_pll_ki = 21000.0f;
    mc_orig.l_max_duty = 0.9f;
    mc_orig.foc_observer_type = 6u; /* FOC_OBSERVER_MXV_LAMBDA_COMP_LIN */
    mc_orig.foc_sat_comp_mode = 3u; /* SAT_COMP_LAMBDA_AND_FACTOR */
    mc_orig.foc_sat_comp = 0.25f;
    mc_orig.foc_motor_ld_lq_diff = 2.5e-6f;
    mc_orig.l_abs_current_max = 130.0f;
    mc_orig.lo_current_min = -45.5f;
    mc_orig.cc_min_current = 0.07f;

    uint8_t buffer[MOTOR_CONFIG_BUFFER_SIZE];
    size_t out_len = 0;
    assert_int_equal(motor_config_serialize(&mc_orig, &app_orig, buffer, sizeof(buffer), &out_len),
                     EDGE_OK);
    /* lo_current_min is a runtime value in the reference and has no place in the byte
     * stream, so a decode must leave whatever the caller had in it alone. A sentinel
     * proves that; comparing against a value we happened to encode would not. */
    mc_restored.lo_current_min = -999.0f;
    assert_int_equal(motor_config_deserialize(&mc_restored, &app_restored, buffer, out_len),
                     EDGE_OK);

    assert_float_equal(mc_restored.foc_current_filter_const, 0.25f, 1e-4f);
    assert_float_equal(mc_restored.foc_pll_kp, 1500.0f, 0.5f);
    assert_float_equal(mc_restored.foc_pll_ki, 21000.0f, 0.5f);
    assert_float_equal(mc_restored.l_max_duty, 0.9f, 1e-4f);
    assert_int_equal(mc_restored.foc_observer_type, 6u);
    assert_int_equal(mc_restored.foc_sat_comp_mode, 3u);
    assert_float_equal(mc_restored.foc_sat_comp, 0.25f, 1e-4f);
    assert_float_equal(mc_restored.foc_motor_ld_lq_diff, 2.5e-6f, 1e-10f);
    assert_float_equal(mc_restored.l_abs_current_max, 130.0f, 1e-2f);
    assert_float_equal(mc_restored.lo_current_min, -999.0f, 1e-6f); /* runtime, untouched */
    assert_float_equal(mc_restored.cc_min_current, 0.07f, 1e-4f);
}

static void test_serialization_roundtrip(void **state) {
    (void)state;
    mc_configuration_t mc_orig, mc_restored;
    app_configuration_t app_orig, app_restored;

    motor_config_set_defaults(&mc_orig, &app_orig);
    mc_orig.current_max = 75.5f;
    mc_orig.foc_current_kp = 0.045f;
    app_orig.controller_id = 42;

    uint8_t buffer[MOTOR_CONFIG_BUFFER_SIZE];
    size_t out_len = 0;

    assert_int_equal(motor_config_serialize(&mc_orig, &app_orig, buffer, sizeof(buffer), &out_len),
                     EDGE_OK);
    assert_true(out_len > 0);

    assert_int_equal(motor_config_deserialize(&mc_restored, &app_restored, buffer, out_len),
                     EDGE_OK);

    assert_int_equal(mc_restored.motor_type, mc_orig.motor_type);
    assert_true(fabsf(mc_restored.current_max - 75.5f) < 0.02f);
    assert_true(fabsf(mc_restored.foc_current_kp - 0.045f) < 1e-4f);
    assert_int_equal(app_restored.controller_id, 42);

    /* Corrupt buffer CRC */
    buffer[4] ^= 0xFF;
    assert_int_equal(motor_config_deserialize(&mc_restored, &app_restored, buffer, out_len),
                     EDGE_EINVAL);
}

static void test_module_lifecycle_and_storage(void **state) {
    (void)state;
    mock_storage_ctx_t ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    ctx.read_count = 0;
    ctx.write_count = 0;
    ctx.erase_count = 0;

    motor_config_storage_port_t storage_port = {
        .read = mock_flash_read,
        .write = mock_flash_write,
        .erase = mock_flash_erase,
        .self = &ctx,
    };

    /* Caller-provided storage: two instances, so two blocks. */
    static alignas(
        MOTOR_CONFIG_STORAGE_ALIGN) unsigned char config_storage[MOTOR_CONFIG_STORAGE_SIZE];
    static alignas(
        MOTOR_CONFIG_STORAGE_ALIGN) unsigned char config2_storage[MOTOR_CONFIG_STORAGE_SIZE];
    motor_config_t *config = (motor_config_t *)config_storage;
    memset(config_storage, 0, sizeof(config_storage));
    memset(config2_storage, 0, sizeof(config2_storage));
    motor_config_construct(config, EDGE_MOD_MOTOR_CONFIG, 20, &storage_port, 0x100);

    /* Init on clean/empty flash should auto-save defaults */
    assert_int_equal(motor_config_init(config), EDGE_OK);
    assert_true(ctx.read_count >= 1);
    assert_true(ctx.write_count >= 1);

    /* Update a parameter */
    mc_configuration_t new_mc = *motor_config_get_mc(config);
    new_mc.current_max = 90.0f;
    assert_int_equal(motor_config_update_mc(config, &new_mc), EDGE_OK);

    /* Polling flushes dirty config to flash */
    edge_module_t *mod = motor_config_module(config);
    assert_non_null(mod);
    assert_int_equal(mod->poll(mod), EDGE_OK);

    /* Create new instance and load from the same mock flash */
    motor_config_t *config2 = (motor_config_t *)config2_storage;
    motor_config_construct(config2, EDGE_MOD_MOTOR_CONFIG, 20, &storage_port, 0x100);
    assert_int_equal(motor_config_init(config2), EDGE_OK);

    const mc_configuration_t *loaded_mc = motor_config_get_mc(config2);
    assert_non_null(loaded_mc);
    assert_true(fabsf(loaded_mc->current_max - 90.0f) < 0.02f);
}

/*
 * Golden vector: the serialised bytes of a default configuration with a few fields
 * moved, captured before the field-manifest refactor. Not one byte may change, so this
 * is what proves the refactor is behaviour-preserving - and it is the baseline the
 * reference's own confgenerator stream gets compared against as C2 fills the list in.
 */
static void test_motor_config_golden_bytes(void **state) {
    (void)state;
    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);
    mc.foc_pll_kp = 1500.0f;
    mc.l_max_duty = 0.9f;
    mc.foc_observer_type = 6u;
    app.can_baud_rate = 250000u;

    static const uint8_t expected[] = {
        0x56, 0x45, 0x53, 0x43, 0x00, 0x06, 0x00, 0x9F, 0x1C, 0xC1, 0x02, 0xFF, 0xFF, 0xE8, 0x90,
        0x00, 0x00, 0x17, 0x70, 0xFF, 0xFF, 0xE8, 0x90, 0x00, 0x00, 0x26, 0xAC, 0x00, 0x00, 0x27,
        0x10, 0x00, 0x00, 0x27, 0x10, 0x00, 0x00, 0x03, 0x20, 0x00, 0x00, 0x15, 0x7C, 0xFF, 0xFE,
        0x79, 0x60, 0x00, 0x01, 0x86, 0xA0, 0x00, 0x00, 0x75, 0x30, 0x00, 0x07, 0xA1, 0x20, 0x00,
        0x00, 0x61, 0xA8, 0x00, 0x00, 0x3A, 0x98, 0x00, 0x00, 0x1B, 0x58, 0x00, 0x00, 0x5F, 0xB4,
        0x00, 0x01, 0x5F, 0x90, 0x00, 0x00, 0x03, 0x52, 0x00, 0x00, 0x03, 0x52, 0x0E, 0x00, 0x00,
        0x75, 0x30, 0x00, 0x00, 0x03, 0x3D, 0x00, 0x00, 0x03, 0xE8, 0x00, 0x00, 0x05, 0xDC, 0x00,
        0x00, 0x75, 0x30, 0x00, 0x00, 0x23, 0x28, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x0F, 0xA0, 0x00, 0x00, 0x0F, 0xA0, 0x00, 0x00, 0x03, 0xE8, 0x00,
        0x00, 0x07, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x61, 0xA8, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0xF4, 0x01, 0x00, 0x00, 0x03, 0xE8, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x03, 0xD0, 0x90};

    uint8_t buf[MOTOR_CONFIG_BUFFER_SIZE];
    size_t n = 0;
    assert_int_equal(motor_config_serialize(&mc, &app, buf, sizeof(buf), &n), EDGE_OK);
    assert_int_equal(n, sizeof(expected));
    assert_memory_equal(buf, expected, sizeof(expected));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_defaults_and_validation),
        cmocka_unit_test(test_controller_fields_have_reference_defaults),
        cmocka_unit_test(test_controller_fields_survive_a_round_trip),
        cmocka_unit_test(test_motor_config_golden_bytes),
        cmocka_unit_test(test_serialization_roundtrip),
        cmocka_unit_test(test_module_lifecycle_and_storage),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
