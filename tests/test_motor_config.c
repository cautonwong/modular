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
#include "vesc_comm/vesc_comm.h" /* vesc_crc16, for the cross-check */

typedef struct mock_storage_ctx {
    uint8_t flash_mem[1536]; /* the module reads its whole 1024-byte scratch at offset 0x100 */
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

    assert_int_equal(mc.motor_type, MOTOR_TYPE_FOC);
    assert_true(mc.l_current_max > 0.0f);
    assert_true(mc.l_current_min < 0.0f);
    assert_true(mc.l_max_vin > mc.l_min_vin);

    /* Test invalid configs */
    mc.l_current_max = -10.0f;
    assert_int_equal(motor_config_validate(&mc, &app), EDGE_EINVAL);

    mc.l_current_max = 50.0f;
    mc.l_max_vin = 5.0f; /* Less than v_in_min */
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
    /* mcconf_default.h: MCCONF_FOC_OBSERVER_TYPE is FOC_OBSERVER_MXLEMMING_LAMBDA_COMP,
     * i.e. 3 - not ORTEGA_ORIGINAL, which this test asserted before the manifest was
     * generated from the reference's own set_defaults_mcconf(). */
    assert_int_equal(mc.foc_observer_type, 3u); /* FOC_OBSERVER_MXLEMMING_LAMBDA_COMP */
    /* Compensation defaults: disabled, factor 0, no saliency difference. */
    assert_int_equal(mc.foc_sat_comp_mode, 0u); /* SAT_COMP_DISABLED */
    assert_float_equal(mc.foc_sat_comp, 0.0f, 1e-6f);
    assert_float_equal(mc.foc_motor_ld_lq_diff, 0.0f, 1e-6f);
    /* cc_min_current and l_abs_current_max both have global reference defaults; only the
     * lo_* values are runtime and stay 0. The macro for the absolute limit is
     * MCCONF_L_MAX_ABS_CURRENT (130 A), not the MCCONF_L_ABS_CURRENT_MAX this file once
     * assumed and reported as "pending the board". */
    assert_float_equal(mc.cc_min_current, 0.05f, 1e-6f);
    assert_float_equal(mc.l_abs_current_max, 130.0f, 1e-6f);
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
    mc_orig.l_current_max = 75.5f;
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
    assert_true(fabsf(mc_restored.l_current_max - 75.5f) < 0.02f);
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
    new_mc.l_current_max = 90.0f;
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
    assert_true(fabsf(loaded_mc->l_current_max - 90.0f) < 0.02f);
}

/*
 * The reference's own mc_configuration stream, byte for byte: a default configuration
 * serialised by confgenerator_serialize_mcconf() in the reference tree (harness and the
 * full vector are in docs/bldc-mcconf-format.md). 488 bytes including the 4-byte
 * signature. Any field that moves, changes scale or changes encoding fails here, which is
 * the point: this is the protocol's contract with VESC Tool, not a self-consistency check.
 */
static void test_motor_config_golden_bytes(void **state) {
    (void)state;
    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);

    static const uint8_t expected[] = {
        0xBC, 0x09, 0xF8, 0xB0, 0x01, 0x00, 0x02, 0x00, 0x42, 0x70, 0x00, 0x00, 0xC2, 0x70, 0x00,
        0x00, 0x42, 0xC6, 0x00, 0x00, 0xC2, 0x70, 0x00, 0x00, 0x23, 0x28, 0x00, 0x14, 0x43, 0x02,
        0x00, 0x00, 0xC7, 0xC3, 0x50, 0x00, 0x47, 0xC3, 0x50, 0x00, 0x1F, 0x40, 0x43, 0x96, 0x00,
        0x00, 0x44, 0xBB, 0x80, 0x00, 0x00, 0x50, 0x02, 0x3A, 0x00, 0x64, 0x00, 0x50, 0x27, 0x10,
        0x2A, 0xF8, 0x00, 0x55, 0x64, 0x55, 0x64, 0x05, 0xDC, 0x00, 0x32, 0x25, 0x1C, 0x49, 0xB7,
        0x1B, 0x00, 0xC9, 0xB7, 0x1B, 0x00, 0x27, 0x10, 0x27, 0x10, 0x27, 0x10, 0x00, 0x43, 0x16,
        0x00, 0x00, 0x44, 0x89, 0x80, 0x00, 0x41, 0x20, 0x00, 0x00, 0x02, 0x6C, 0x1F, 0x40, 0x47,
        0x9C, 0x40, 0x00, 0x44, 0x16, 0x00, 0x00, 0xFF, 0x01, 0x03, 0x02, 0x05, 0x06, 0x04, 0xFF,
        0x44, 0xFA, 0x00, 0x00, 0x3C, 0xF5, 0xC2, 0x8F, 0x42, 0x48, 0x00, 0x00, 0x46, 0xC3, 0x50,
        0x00, 0x3D, 0xF5, 0xC2, 0x8F, 0x00, 0x43, 0x34, 0x00, 0x00, 0x40, 0xE0, 0x00, 0x00, 0x00,
        0x44, 0xFA, 0x00, 0x00, 0x46, 0xEA, 0x60, 0x00, 0x36, 0xEA, 0xE1, 0x8B, 0x00, 0x00, 0x00,
        0x00, 0x3C, 0x75, 0xC2, 0x8F, 0x3B, 0x20, 0x90, 0x2E, 0x49, 0x5B, 0xBA, 0x00, 0x3D, 0x4C,
        0xCC, 0xCD, 0xFC, 0x18, 0x41, 0xA0, 0x00, 0x00, 0x43, 0xC8, 0x00, 0x00, 0x27, 0x10, 0x45,
        0x1C, 0x40, 0x00, 0x44, 0xBB, 0x80, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A,
        0x00, 0x05, 0x00, 0x00, 0xFF, 0x9C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x43,
        0xFA, 0x00, 0x00, 0x45, 0x1C, 0x40, 0x00, 0x45, 0x5A, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x09, 0xC4, 0x03, 0xE8, 0x00, 0x03, 0x00, 0x02, 0x58, 0x0F, 0x00, 0xC8, 0x00,
        0x28, 0x00, 0x3C, 0x01, 0x2C, 0x01, 0x2C, 0x00, 0x00, 0x45, 0x3B, 0x80, 0x00, 0x43, 0xFA,
        0x00, 0x00, 0x00, 0x05, 0x3A, 0x83, 0x12, 0x6F, 0x01, 0x01, 0x45, 0x00, 0x00, 0x00, 0x45,
        0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x45, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1F, 0x40, 0x00, 0x00, 0x01, 0xF4, 0x07, 0xD0, 0x00, 0x00, 0x27, 0x10, 0x26, 0x48, 0x05,
        0x3B, 0x83, 0x12, 0x6F, 0x3B, 0x83, 0x12, 0x6F, 0x38, 0xD1, 0xB7, 0x17, 0x07, 0xD0, 0x44,
        0x61, 0x00, 0x00, 0x01, 0x46, 0xC3, 0x50, 0x00, 0x00, 0x3C, 0xCC, 0xCC, 0xCD, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x39, 0xB7, 0x80, 0x34, 0x07, 0xD0, 0x3F, 0x80, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x3D, 0x4C, 0xCC, 0xCD, 0x3B, 0x96,
        0xBB, 0x99, 0x01, 0x90, 0x00, 0x00, 0x01, 0xF4, 0x00, 0xC8, 0x3F, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x20, 0x00, 0x03, 0xE8, 0x03, 0xE8, 0x06, 0x72, 0x06, 0x72, 0x01, 0xF4, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x10, 0x45, 0x3B, 0x80, 0x00, 0x47, 0x08, 0xB8, 0x00, 0x46, 0xC3, 0x50,
        0x00, 0x45, 0x53, 0x40, 0x00, 0x00, 0x00, 0x3F, 0x1C, 0x28, 0xF6, 0x03, 0xE8, 0x00, 0xFA,
        0x03, 0x2D, 0x0E, 0x40, 0x40, 0x00, 0x00, 0x3D, 0xA9, 0xFB, 0xE7, 0x00, 0x03, 0x40, 0xC0,
        0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x01, 0x03, 0x2D, 0x41, 0x00, 0x32, 0x00, 0x00, 0x0B,
        0x54, 0x09, 0xC4, 0x10, 0x68, 0x10, 0xCC, 0x00};

    uint8_t buf[MOTOR_CONFIG_BUFFER_SIZE];
    size_t n = 0;
    assert_int_equal(motor_config_stream_len(), sizeof(expected));
    assert_int_equal(motor_config_serialize_mc(&mc, buf, sizeof(buf), &n), EDGE_OK);
    assert_int_equal(n, sizeof(expected));
    assert_memory_equal(buf, expected, sizeof(expected));

    /* And it reads back into the same values. */
    mc_configuration_t back;
    memset(&back, 0, sizeof(back));
    assert_int_equal(motor_config_deserialize_mc(&back, buf, n), EDGE_OK);
    assert_float_equal(back.l_current_max, mc.l_current_max, 1e-6f);
    assert_float_equal(back.foc_pll_kp, mc.foc_pll_kp, 1e-6f);
    assert_float_equal(back.foc_observer_gain, mc.foc_observer_gain, 1e-3f);
    assert_int_equal(back.foc_observer_type, mc.foc_observer_type);
    assert_float_equal(back.foc_motor_l, mc.foc_motor_l, 1e-9f);
}

/*
 * The same contract with fields pushed off their defaults, so the encoders are exercised
 * on values that are neither zero nor the reference's defaults: a negative current, a
 * small inductance, a large observer gain, a different pole count, a different pwm_mode
 * and a bms limit. Bytes from the reference's own serialiser, same harness as above.
 */
static void test_motor_config_golden_bytes_off_default(void **state) {
    (void)state;
    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);
    mc.l_current_max = 12.5f;
    mc.l_current_min = -33.25f;
    mc.foc_motor_l = 1.23e-5f;
    mc.foc_motor_flux_linkage = 0.001234f;
    mc.foc_observer_gain = 123456.0f;
    mc.si_motor_poles = 28;
    mc.pwm_mode = 1;
    mc.bms.vmax_limit_start = 4.2f;

    static const uint8_t expected[] = {
        0xBC, 0x09, 0xF8, 0xB0, 0x01, 0x00, 0x02, 0x00, 0x41, 0x48, 0x00, 0x00, 0xC2, 0x05, 0x00,
        0x00, 0x42, 0xC6, 0x00, 0x00, 0xC2, 0x70, 0x00, 0x00, 0x23, 0x28, 0x00, 0x14, 0x43, 0x02,
        0x00, 0x00, 0xC7, 0xC3, 0x50, 0x00, 0x47, 0xC3, 0x50, 0x00, 0x1F, 0x40, 0x43, 0x96, 0x00,
        0x00, 0x44, 0xBB, 0x80, 0x00, 0x00, 0x50, 0x02, 0x3A, 0x00, 0x64, 0x00, 0x50, 0x27, 0x10,
        0x2A, 0xF8, 0x00, 0x55, 0x64, 0x55, 0x64, 0x05, 0xDC, 0x00, 0x32, 0x25, 0x1C, 0x49, 0xB7,
        0x1B, 0x00, 0xC9, 0xB7, 0x1B, 0x00, 0x27, 0x10, 0x27, 0x10, 0x27, 0x10, 0x00, 0x43, 0x16,
        0x00, 0x00, 0x44, 0x89, 0x80, 0x00, 0x41, 0x20, 0x00, 0x00, 0x02, 0x6C, 0x1F, 0x40, 0x47,
        0x9C, 0x40, 0x00, 0x44, 0x16, 0x00, 0x00, 0xFF, 0x01, 0x03, 0x02, 0x05, 0x06, 0x04, 0xFF,
        0x44, 0xFA, 0x00, 0x00, 0x3C, 0xF5, 0xC2, 0x8F, 0x42, 0x48, 0x00, 0x00, 0x46, 0xC3, 0x50,
        0x00, 0x3D, 0xF5, 0xC2, 0x8F, 0x00, 0x43, 0x34, 0x00, 0x00, 0x40, 0xE0, 0x00, 0x00, 0x00,
        0x44, 0xFA, 0x00, 0x00, 0x46, 0xEA, 0x60, 0x00, 0x37, 0x4E, 0x5C, 0x19, 0x00, 0x00, 0x00,
        0x00, 0x3C, 0x75, 0xC2, 0x8F, 0x3A, 0xA1, 0xBE, 0x2B, 0x47, 0xF1, 0x20, 0x00, 0x3D, 0x4C,
        0xCC, 0xCD, 0xFC, 0x18, 0x41, 0xA0, 0x00, 0x00, 0x43, 0xC8, 0x00, 0x00, 0x27, 0x10, 0x45,
        0x1C, 0x40, 0x00, 0x44, 0xBB, 0x80, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00, 0x0A,
        0x00, 0x05, 0x00, 0x00, 0xFF, 0x9C, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x43,
        0xFA, 0x00, 0x00, 0x45, 0x1C, 0x40, 0x00, 0x45, 0x5A, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x09, 0xC4, 0x03, 0xE8, 0x00, 0x03, 0x00, 0x02, 0x58, 0x0F, 0x00, 0xC8, 0x00,
        0x28, 0x00, 0x3C, 0x01, 0x2C, 0x01, 0x2C, 0x00, 0x00, 0x45, 0x3B, 0x80, 0x00, 0x43, 0xFA,
        0x00, 0x00, 0x00, 0x05, 0x3A, 0x83, 0x12, 0x6F, 0x01, 0x01, 0x45, 0x00, 0x00, 0x00, 0x45,
        0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x45, 0x7A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x1F, 0x40, 0x00, 0x00, 0x01, 0xF4, 0x07, 0xD0, 0x00, 0x00, 0x27, 0x10, 0x26, 0x48, 0x05,
        0x3B, 0x83, 0x12, 0x6F, 0x3B, 0x83, 0x12, 0x6F, 0x38, 0xD1, 0xB7, 0x17, 0x07, 0xD0, 0x44,
        0x61, 0x00, 0x00, 0x01, 0x46, 0xC3, 0x50, 0x00, 0x00, 0x3C, 0xCC, 0xCC, 0xCD, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x39, 0xB7, 0x80, 0x34, 0x07, 0xD0, 0x3F, 0x80, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x3D, 0x4C, 0xCC, 0xCD, 0x3B, 0x96,
        0xBB, 0x99, 0x01, 0x90, 0x00, 0x00, 0x01, 0xF4, 0x00, 0xC8, 0x3F, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x20, 0x00, 0x03, 0xE8, 0x03, 0xE8, 0x06, 0x72, 0x06, 0x72, 0x01, 0xF4, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x10, 0x45, 0x3B, 0x80, 0x00, 0x47, 0x08, 0xB8, 0x00, 0x46, 0xC3, 0x50,
        0x00, 0x45, 0x53, 0x40, 0x00, 0x00, 0x00, 0x3F, 0x1C, 0x28, 0xF6, 0x03, 0xE8, 0x00, 0xFA,
        0x03, 0x2D, 0x1C, 0x40, 0x40, 0x00, 0x00, 0x3D, 0xA9, 0xFB, 0xE7, 0x00, 0x03, 0x40, 0xC0,
        0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x01, 0x03, 0x2D, 0x41, 0x00, 0x32, 0x00, 0x00, 0x0B,
        0x54, 0x09, 0xC4, 0x10, 0x68, 0x10, 0xCC, 0x00};

    uint8_t buf[MOTOR_CONFIG_BUFFER_SIZE];
    size_t n = 0;
    assert_int_equal(motor_config_serialize_mc(&mc, buf, sizeof(buf), &n), EDGE_OK);
    assert_int_equal(n, sizeof(expected));
    assert_memory_equal(buf, expected, sizeof(expected));

    mc_configuration_t back;
    memset(&back, 0, sizeof(back));
    assert_int_equal(motor_config_deserialize_mc(&back, buf, n), EDGE_OK);
    assert_float_equal(back.l_current_max, 12.5f, 1e-6f);
    assert_float_equal(back.l_current_min, -33.25f, 1e-6f);
    assert_float_equal(back.foc_motor_l, 1.23e-5f, 1e-9f);
    assert_float_equal(back.foc_observer_gain, 123456.0f, 1e-1f);
    assert_int_equal(back.si_motor_poles, 28u);
    assert_int_equal(back.pwm_mode, 1);
    assert_float_equal(back.bms.vmax_limit_start, 4.2f, 1e-3f);
}

/*
 * The app_configuration stream, same contract as the mc one: the reference's own
 * confgenerator_serialize_appconf() output for a default configuration, 290 bytes
 * including the signature, from the same harness.
 */
static void test_motor_config_app_golden_bytes(void **state) {
    (void)state;
    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);

    static const uint8_t expected[] = {
        0x11, 0xAD, 0xA6, 0xCC, 0x01, 0x00, 0x00, 0x03, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32,
        0x00, 0x05, 0x00, 0x00, 0x02, 0x00, 0x01, 0x07, 0x00, 0x00, 0x00, 0x47, 0x43, 0x50, 0x00,
        0x00, 0x00, 0x00, 0x03, 0x00, 0x46, 0x6A, 0x60, 0x00, 0x3E, 0x19, 0x99, 0x9A, 0x3F, 0x80,
        0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x3F, 0xC0, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x3E, 0xCC, 0xCC, 0xCD, 0x3E, 0x4C, 0xCC, 0xCD, 0x01,
        0x00, 0x45, 0x3B, 0x80, 0x00, 0x0F, 0xA0, 0x3D, 0x8F, 0x5C, 0x29, 0x40, 0x40, 0x00, 0x00,
        0x00, 0x3E, 0x19, 0x99, 0x9A, 0x03, 0x84, 0x0B, 0xB8, 0x00, 0x00, 0x0D, 0xAC, 0x07, 0xD0,
        0x03, 0x84, 0x0B, 0xB8, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x02, 0x3E, 0x99, 0x99, 0x9A, 0x3D, 0xCC, 0xCC, 0xCD, 0x01, 0x00, 0x45, 0x3B,
        0x80, 0x00, 0x01, 0xF4, 0x00, 0x00, 0x3F, 0x80, 0x00, 0x00, 0x00, 0x01, 0xC2, 0x00, 0x00,
        0x3E, 0x19, 0x99, 0x9A, 0x3E, 0xCC, 0xCC, 0xCD, 0x3E, 0x4C, 0xCC, 0xCD, 0x45, 0x3B, 0x80,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01, 0x00, 0x45, 0x3B, 0x80,
        0x00, 0x01, 0x3D, 0x8F, 0x5C, 0x29, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x3F, 0x80, 0x00,
        0x00, 0x01, 0x03, 0x01, 0x00, 0x03, 0x4C, 0xC6, 0xC7, 0x00, 0x01, 0x00, 0x00, 0x00, 0x64,
        0x00, 0x64, 0x07, 0x08, 0x00, 0x00, 0x18, 0x01, 0x00, 0x3C, 0x00, 0x1E, 0x01, 0xF4, 0x01,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, 0x01, 0x3F, 0x80,
        0x00, 0x00, 0x3E, 0x99, 0x99, 0x9A, 0x00, 0x00, 0x00, 0x00, 0x3D, 0xCC, 0xCC, 0xCD, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00};

    /* Defaults the generator took from the reference: controller_id is the board's
     * HW_DEFAULT_ID (1 when no board supplies one), the timeout 1000 ms, and the baud rate
     * is the CAN_BAUD enum's 500k member (2) - not the 500000 this port used to write,
     * which was a baud number where the reference has an enum. */
    assert_int_equal(app.controller_id, 1u);
    assert_int_equal(app.timeout_msec, 1000u);
    assert_int_equal(app.can_baud_rate, 2);

    uint8_t buf[MOTOR_CONFIG_BUFFER_SIZE];
    size_t n = 0;
    assert_int_equal(motor_config_app_stream_len(), sizeof(expected));
    assert_int_equal(motor_config_serialize_app(&app, buf, sizeof(buf), &n), EDGE_OK);
    assert_int_equal(n, sizeof(expected));
    assert_memory_equal(buf, expected, sizeof(expected));

    app_configuration_t back;
    memset(&back, 0, sizeof(back));
    assert_int_equal(motor_config_deserialize_app(&back, buf, n), EDGE_OK);
    assert_int_equal(back.controller_id, app.controller_id);
    assert_int_equal(back.timeout_msec, app.timeout_msec);
    assert_float_equal(back.timeout_brake_current, app.timeout_brake_current, 1e-6f);
}

/*
 * COMM_GET_MCCONF_DEFAULT: the reference's defaults, except the nine calibration offsets,
 * which it takes from the live configuration so a peer cannot wipe a motor's measured
 * calibration by asking for the defaults. A test is the only way to see that, since the
 * offsets are not visible in the byte layout's field names.
 */
static void test_motor_config_defaults_keep_the_calibration_offsets(void **state) {
    (void)state;
    mock_storage_ctx_t ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    motor_config_storage_port_t storage_port = {
        .read = mock_flash_read,
        .write = mock_flash_write,
        .erase = mock_flash_erase,
        .self = &ctx,
    };
    static alignas(MOTOR_CONFIG_STORAGE_ALIGN) unsigned char storage[MOTOR_CONFIG_STORAGE_SIZE];
    memset(storage, 0, sizeof(storage));
    motor_config_t *cfg = (motor_config_t *)storage;
    motor_config_construct(cfg, EDGE_MOD_MOTOR_CONFIG, 20u, &storage_port, 0x100u);
    assert_int_equal(motor_config_init(cfg), EDGE_OK);

    /* Live calibration, plus a non-default current limit to show what is NOT carried over. */
    mc_configuration_t live = *motor_config_get_mc(cfg);
    for (size_t i = 0u; i < 3u; i++) {
        /* The voltage offsets travel as float16 fixed point, whose range is a few volts:
         * values past it wrap, which is the reference's behaviour too, so the test stays
         * inside it. Only the "carried over" part is under test here. */
        live.foc_offsets_current[i] = 0.25f * (float)(i + 1u);
        live.foc_offsets_voltage[i] = 0.15f * (float)(i + 1u);
        live.foc_offsets_voltage_undriven[i] = 0.25f * (float)(i + 1u);
    }
    live.l_current_max = 42.0f;
    assert_int_equal(motor_config_update_mc(cfg, &live), EDGE_OK);

    uint8_t buf[MOTOR_CONFIG_BUFFER_SIZE];
    size_t n = 0;
    assert_int_equal(motor_config_serialize_mc_defaults(cfg, buf, sizeof(buf), &n), EDGE_OK);

    mc_configuration_t back;
    memset(&back, 0, sizeof(back));
    assert_int_equal(motor_config_deserialize_mc(&back, buf, n), EDGE_OK);
    for (size_t i = 0u; i < 3u; i++) {
        assert_float_equal(back.foc_offsets_current[i], live.foc_offsets_current[i], 1e-3f);
        assert_float_equal(back.foc_offsets_voltage[i], live.foc_offsets_voltage[i], 1e-3f);
        assert_float_equal(back.foc_offsets_voltage_undriven[i],
                           live.foc_offsets_voltage_undriven[i], 1e-3f);
    }

    mc_configuration_t mc_defaults;
    app_configuration_t app_defaults;
    motor_config_set_defaults(&mc_defaults, &app_defaults);
    assert_float_equal(back.l_current_max, mc_defaults.l_current_max, 1e-3f);
    assert_true(back.l_current_max != live.l_current_max);
}

/*
 * COMM_SET_APPCONF_NO_STORE applies to the running system and leaves the module clean, so a
 * later save does not pick it up. The plain SET does mark it dirty - that difference is the
 * whole point of the variant, so both are asserted here.
 */
static void test_motor_config_app_nostore_applies_without_marking_dirty(void **state) {
    (void)state;
    mock_storage_ctx_t ctx;
    memset(&ctx, 0xFF, sizeof(ctx));
    motor_config_storage_port_t storage_port = {
        .read = mock_flash_read,
        .write = mock_flash_write,
        .erase = mock_flash_erase,
        .self = &ctx,
    };
    static alignas(MOTOR_CONFIG_STORAGE_ALIGN) unsigned char storage[MOTOR_CONFIG_STORAGE_SIZE];
    memset(storage, 0, sizeof(storage));
    motor_config_t *cfg = (motor_config_t *)storage;
    motor_config_construct(cfg, EDGE_MOD_MOTOR_CONFIG, 20u, &storage_port, 0x100u);
    assert_int_equal(motor_config_init(cfg), EDGE_OK);

    app_configuration_t app = *motor_config_get_app(cfg);
    app.timeout_msec = 2500u;
    uint8_t buf[MOTOR_CONFIG_BUFFER_SIZE];
    size_t n = 0;
    assert_int_equal(motor_config_serialize_app(&app, buf, sizeof(buf), &n), EDGE_OK);

    assert_int_equal(motor_config_apply_app_stream_nostore(cfg, buf, n), EDGE_OK);
    assert_int_equal(motor_config_get_app(cfg)->timeout_msec, 2500u);
    assert_false(motor_config_is_dirty(cfg));

    app.timeout_msec = 3000u;
    assert_int_equal(motor_config_serialize_app(&app, buf, sizeof(buf), &n), EDGE_OK);
    assert_int_equal(motor_config_apply_app_stream(cfg, buf, n), EDGE_OK);
    assert_int_equal(motor_config_get_app(cfg)->timeout_msec, 3000u);
    assert_true(motor_config_is_dirty(cfg));
}

/* A mock variable store: one uint16 per two configuration bytes. */
#define MOCK_VAR_COUNT 512u

typedef struct mock_var_store {
    uint16_t values[MOCK_VAR_COUNT];
    bool present; /* false simulates a variable that was never written */
    int writes;
} mock_var_store_t;

static edge_status_t mock_var_read(void *self, uint16_t index, uint16_t *value) {
    mock_var_store_t *store = (mock_var_store_t *)self;
    if (!store->present || index >= MOCK_VAR_COUNT) {
        return EDGE_ENOENT;
    }
    *value = store->values[index];
    return EDGE_OK;
}

static edge_status_t mock_var_write(void *self, uint16_t index, uint16_t value) {
    mock_var_store_t *store = (mock_var_store_t *)self;
    if (index >= MOCK_VAR_COUNT) {
        return EDGE_EINVAL;
    }
    store->values[index] = value;
    store->writes++;
    return EDGE_OK;
}

/*
 * The configuration through the variable store, which is how the reference actually persists it
 * (conf_general.c:436-520): one uint16 per two bytes, and the struct's own crc member as the
 * integrity check rather than an outside envelope.
 */
static void test_motor_config_variable_store(void **state) {
    (void)state;
    mock_var_store_t store;
    memset(&store, 0, sizeof(store));
    store.present = true;
    const motor_config_var_port_t port = {
        .read = mock_var_read, .write = mock_var_write, .self = &store};

    static alignas(MOTOR_CONFIG_STORAGE_ALIGN) unsigned char storage[MOTOR_CONFIG_STORAGE_SIZE];
    memset(storage, 0, sizeof(storage));
    motor_config_t *cfg = (motor_config_t *)storage;
    mock_storage_ctx_t flash;
    memset(&flash, 0xFF, sizeof(flash));
    motor_config_storage_port_t flash_port = {.read = mock_flash_read,
                                              .write = mock_flash_write,
                                              .erase = mock_flash_erase,
                                              .self = &flash};
    motor_config_construct(cfg, EDGE_MOD_MOTOR_CONFIG, 20u, &flash_port, 0x100u);

    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);
    mc.l_current_max = 37.5f;
    mc.foc_pll_kp = 1234.0f;
    assert_int_equal(motor_config_update_mc(cfg, &mc), EDGE_OK);

    assert_int_equal(motor_config_store_to_vars(cfg, &port), EDGE_OK);
    assert_true(store.writes > 0);

    /* Wipe the live configuration, then read it back out of the variables. */
    motor_config_set_defaults(&mc, &app); /* returns void */
    assert_int_equal(motor_config_update_mc(cfg, &mc), EDGE_OK);
    assert_float_equal(motor_config_get_mc(cfg)->l_current_max, 60.0f, 1e-3f);

    assert_int_equal(motor_config_load_from_vars(cfg, &port), EDGE_OK);
    assert_float_equal(motor_config_get_mc(cfg)->l_current_max, 37.5f, 1e-2f);
    assert_float_equal(motor_config_get_mc(cfg)->foc_pll_kp, 1234.0f, 1e-1f);

    /* A variable that never made it falls back to the defaults, as the reference does. */
    store.present = false;
    assert_int_equal(motor_config_load_from_vars(cfg, &port), EDGE_ENOENT);
    assert_float_equal(motor_config_get_mc(cfg)->l_current_max, 60.0f, 1e-3f);

    /* And so does a CRC mismatch. */
    store.present = true;
    store.values[1] ^= 0xFFFFu;
    assert_int_equal(motor_config_load_from_vars(cfg, &port), EDGE_EINVAL);
    assert_float_equal(motor_config_get_mc(cfg)->l_current_max, 60.0f, 1e-3f);
}

/*
 * The CRC this module computes must be the one the codec's table-driven vesc_crc16 computes, or
 * the two copies of util/crc.c's algorithm have drifted. The reference's own version zeroes the
 * crc member and covers the whole struct, padding included, so the check mirrors that.
 */
static void test_motor_config_crc_matches_the_codec(void **state) {
    (void)state;
    mc_configuration_t mc;
    app_configuration_t app;
    motor_config_set_defaults(&mc, &app);
    mc.l_current_max = 12.5f;

    const uint16_t mine = motor_config_config_crc(&mc);
    const uint16_t saved = mc.crc;
    mc.crc = 0u;
    const uint16_t theirs = vesc_crc16((const uint8_t *)&mc, sizeof(mc));
    mc.crc = saved;

    assert_int_equal(mine, theirs);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_defaults_and_validation),
        cmocka_unit_test(test_controller_fields_have_reference_defaults),
        cmocka_unit_test(test_controller_fields_survive_a_round_trip),
        cmocka_unit_test(test_motor_config_golden_bytes),
        cmocka_unit_test(test_motor_config_golden_bytes_off_default),
        cmocka_unit_test(test_motor_config_app_golden_bytes),
        cmocka_unit_test(test_motor_config_variable_store),
        cmocka_unit_test(test_motor_config_crc_matches_the_codec),
        cmocka_unit_test(test_motor_config_defaults_keep_the_calibration_offsets),
        cmocka_unit_test(test_motor_config_app_nostore_applies_without_marking_dirty),
        cmocka_unit_test(test_serialization_roundtrip),
        cmocka_unit_test(test_module_lifecycle_and_storage),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
