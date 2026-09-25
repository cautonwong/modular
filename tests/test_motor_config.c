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

    uint8_t buffer[MOTOR_CONFIG_BUFFER_SIZE];
    size_t out_len = 0;
    assert_int_equal(motor_config_serialize(&mc_orig, &app_orig, buffer, sizeof(buffer), &out_len),
                     EDGE_OK);
    assert_int_equal(motor_config_deserialize(&mc_restored, &app_restored, buffer, out_len),
                     EDGE_OK);

    assert_float_equal(mc_restored.foc_current_filter_const, 0.25f, 1e-4f);
    assert_float_equal(mc_restored.foc_pll_kp, 1500.0f, 0.5f);
    assert_float_equal(mc_restored.foc_pll_ki, 21000.0f, 0.5f);
    assert_float_equal(mc_restored.l_max_duty, 0.9f, 1e-4f);
    assert_int_equal(mc_restored.foc_observer_type, 6u);
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
    static alignas(MOTOR_CONFIG_STORAGE_ALIGN)
        unsigned char config_storage[MOTOR_CONFIG_STORAGE_SIZE];
    static alignas(MOTOR_CONFIG_STORAGE_ALIGN)
        unsigned char config2_storage[MOTOR_CONFIG_STORAGE_SIZE];
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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_defaults_and_validation),
        cmocka_unit_test(test_controller_fields_have_reference_defaults),
        cmocka_unit_test(test_controller_fields_survive_a_round_trip),
        cmocka_unit_test(test_serialization_roundtrip),
        cmocka_unit_test(test_module_lifecycle_and_storage),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
