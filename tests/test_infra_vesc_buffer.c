/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include <cmocka.h>
/* clang-format on */

#include "vesc_buffer/buffer.h"

static void test_vesc_buffer_integers(void **state) {
    (void)state;
    uint8_t buf[64] = {0};
    int32_t ind = 0;

    vesc_buffer_append_int16(buf, -1234, &ind);
    vesc_buffer_append_uint16(buf, 0xABCD, &ind);
    vesc_buffer_append_int32(buf, -987654, &ind);
    vesc_buffer_append_uint32(buf, 0x12345678, &ind);
    vesc_buffer_append_int64(buf, -1234567890123LL, &ind);
    vesc_buffer_append_uint64(buf, 0xAABBCCDDEEFF0011ULL, &ind);

    int32_t r_ind = 0;
    assert_int_equal(vesc_buffer_get_int16(buf, &r_ind), -1234);
    assert_int_equal(vesc_buffer_get_uint16(buf, &r_ind), 0xABCD);
    assert_int_equal(vesc_buffer_get_int32(buf, &r_ind), -987654);
    assert_int_equal(vesc_buffer_get_uint32(buf, &r_ind), 0x12345678);
    assert_true(vesc_buffer_get_int64(buf, &r_ind) == -1234567890123LL);
    assert_true(vesc_buffer_get_uint64(buf, &r_ind) == 0xAABBCCDDEEFF0011ULL);
    assert_int_equal(r_ind, ind);
}

static void test_vesc_buffer_floats(void **state) {
    (void)state;
    uint8_t buf[64] = {0};
    int32_t ind = 0;

    vesc_buffer_append_float16(buf, 24.5f, 10.0f, &ind);
    vesc_buffer_append_float32(buf, 123.456f, 1000.0f, &ind);
    vesc_buffer_append_double64(buf, 4567.89, 100.0, &ind);
    vesc_buffer_append_float32_auto(buf, 3.14159f, &ind);

    int32_t r_ind = 0;
    float f16 = vesc_buffer_get_float16(buf, 10.0f, &r_ind);
    assert_true(fabsf(f16 - 24.5f) < 0.1f);

    float f32 = vesc_buffer_get_float32(buf, 1000.0f, &r_ind);
    assert_true(fabsf(f32 - 123.456f) < 0.002f);

    double d64 = vesc_buffer_get_double64(buf, 100.0, &r_ind);
    assert_true(fabs(d64 - 4567.89) < 0.02);

    float f_auto = vesc_buffer_get_float32_auto(buf, &r_ind);
    assert_true(fabsf(f_auto - 3.14159f) < 0.001f);
}

/*
 * The "auto" encoding is a wire contract with VESC Tool, not an internal detail:
 * a round-trip through this module's own decoder passes for any self-consistent
 * encoder, so the bytes are pinned to the reference firmware's output instead.
 * These are the reference firmware's values (util/buffer.c, dumped by
 * running it), which happen to be the IEEE-754 single-precision bit patterns.
 */
static void test_vesc_buffer_float32_auto_wire_format(void **state) {
    (void)state;
    static const struct {
        float value;
        uint32_t wire;
    } golden[] = {
        {1.0f, 0x3F800000u},     {-1.0f, 0xBF800000u}, {0.5f, 0x3F000000u},
        {2.0f, 0x40000000u},     {0.1f, 0x3DCCCCCDu},  {-12345.678f, 0xC640E6B6u},
        {3.14159f, 0x40490FD0u},
    };

    for (size_t i = 0; i < sizeof golden / sizeof golden[0]; i++) {
        uint8_t buf[8] = {0};
        int32_t ind = 0;
        vesc_buffer_append_float32_auto(buf, golden[i].value, &ind);
        assert_int_equal(ind, 4);
        uint32_t got = ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
                       ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
        assert_int_equal(got, golden[i].wire);

        int32_t r_ind = 0;
        assert_float_equal(vesc_buffer_get_float32_auto(buf, &r_ind), golden[i].value, 1e-6f);
        assert_int_equal(r_ind, 4);
    }

    /* Subnormals are flushed, as the reference does. */
    uint8_t buf[8] = {0};
    int32_t ind = 0;
    vesc_buffer_append_float32_auto(buf, 1e-40f, &ind);
    for (int i = 0; i < 4; i++) {
        assert_int_equal(buf[i], 0u);
    }
}

static void test_vesc_buffer_float64_auto(void **state) {
    (void)state;
    static const double values[] = {1.0, -1.0, 4567.89, -0.0625, 1e30};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        uint8_t buf[16] = {0};
        int32_t ind = 0;
        vesc_buffer_append_float64_auto(buf, values[i], &ind);
        assert_int_equal(ind, 8); /* two float32_auto halves */

        int32_t r_ind = 0;
        assert_true(fabs(vesc_buffer_get_float64_auto(buf, &r_ind) - values[i]) <
                    fabs(values[i]) * 1e-6 + 1e-6);
        assert_int_equal(r_ind, 8);
    }
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_buffer_integers),
        cmocka_unit_test(test_vesc_buffer_floats),
        cmocka_unit_test(test_vesc_buffer_float32_auto_wire_format),
        cmocka_unit_test(test_vesc_buffer_float64_auto),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
