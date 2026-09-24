#include "vesc_buffer/buffer.h"
#include <cmocka.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_vesc_buffer_integers),
        cmocka_unit_test(test_vesc_buffer_floats),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
