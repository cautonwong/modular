/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>

#include <cmocka.h>
/* clang-format on */

#include "encoder/encoder.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

static edge_status_t mock_spi_transfer(void *ctx, uint16_t tx_val, uint16_t *rx_val) {
    (void)ctx;
    (void)tx_val;
    /* Return a valid angle with valid even parity */
    uint16_t angle = 4096; /* 90 deg */
    uint16_t count = 0;
    for (int i = 0; i < 14; i++) {
        if (angle & (1u << i)) {
            count++;
        }
    }
    if ((count % 2) != 0) {
        angle |= 0x8000; /* parity bit */
    }
    *rx_val = angle;
    return EDGE_OK;
}

static void test_encoder_as5047(void **state) {
    (void)state;
    encoder_as5047_t enc;
    encoder_as5047_construct(&enc, mock_spi_transfer, NULL);
    assert_int_equal(encoder_as5047_init(&enc), EDGE_OK);

    float angle_rad = 0.0f;
    assert_int_equal(encoder_as5047_read_angle_rad(&enc, &angle_rad), EDGE_OK);
    assert_true(fabsf(angle_rad - (float)M_PI / 2.0f) < 0.01f);
}

static void test_encoder_mt6816(void **state) {
    (void)state;
    encoder_mt6816_t enc;
    encoder_mt6816_construct(&enc, mock_spi_transfer, NULL);
    assert_int_equal(encoder_mt6816_init(&enc), EDGE_OK);

    float angle_rad = 0.0f;
    assert_int_equal(encoder_mt6816_read_angle_rad(&enc, &angle_rad), EDGE_OK);
}

static void test_encoder_abi(void **state) {
    (void)state;
    encoder_abi_t enc;
    encoder_abi_construct(&enc, 4000);
    assert_int_equal(encoder_abi_init(&enc), EDGE_OK);

    float angle_rad = 0.0f;
    assert_int_equal(encoder_abi_update(&enc, 1000, 0.01f, &angle_rad), EDGE_OK);
    assert_true(fabsf(angle_rad - (float)M_PI / 2.0f) < 0.01f);
}

static void test_encoder_sincos(void **state) {
    (void)state;
    encoder_sincos_t enc;
    encoder_sincos_construct(&enc, 0.0f, 0.0f, 1.0f, 1.0f);
    assert_int_equal(encoder_sincos_init(&enc), EDGE_OK);

    float angle_rad = 0.0f;
    assert_int_equal(encoder_sincos_update(&enc, 1.0f, 0.0f, &angle_rad), EDGE_OK);
    assert_true(fabsf(angle_rad - (float)M_PI / 2.0f) < 0.01f);
}

static void test_encoder_hall(void **state) {
    (void)state;
    encoder_hall_t hall;
    encoder_hall_construct(&hall, NULL);
    assert_int_equal(encoder_hall_init(&hall), EDGE_OK);

    float angle = 0.0f;
    assert_int_equal(encoder_hall_update(&hall, 1, &angle), EDGE_OK);
    assert_true(angle >= 0.0f);
}

/*
 * The paths the per-type cases miss: the resolvers' and the hall reader's construct/init guards,
 * the sincos gains being replaced rather than left at zero (a zero gain is what a division would
 * later divide by), the hall table in its default and custom forms, and the illegal hall state
 * that has to be refused rather than turned into an angle.
 */
static void test_encoder_resolver_and_hall_guards(void **state) {
    (void)state;

    encoder_sincos_t sc;
    encoder_sincos_construct(NULL, 0.0f, 0.0f, 0.0f, 0.0f);
    encoder_sincos_construct(&sc, 0.1f, -0.1f, 0.0f, 0.0f);
    assert_float_equal(sc.sin_gain, 1.0f, 1e-6f);
    assert_float_equal(sc.cos_gain, 1.0f, 1e-6f);
    assert_float_equal(sc.sin_offset, 0.1f, 1e-6f);
    assert_float_equal(sc.cos_offset, -0.1f, 1e-6f);
    assert_int_equal(encoder_sincos_init(NULL), EDGE_EINVAL);
    assert_int_equal(encoder_sincos_init(&sc), EDGE_OK);

    /* A non-zero gain is kept as given. */
    encoder_sincos_t gained;
    encoder_sincos_construct(&gained, 0.0f, 0.0f, 2.0f, 0.5f);
    assert_float_equal(gained.sin_gain, 2.0f, 1e-6f);
    assert_float_equal(gained.cos_gain, 0.5f, 1e-6f);

    encoder_hall_t hall;
    encoder_hall_construct(NULL, NULL);
    assert_int_equal(encoder_hall_init(NULL), EDGE_EINVAL);
    encoder_hall_construct(&hall, NULL);
    assert_int_equal(encoder_hall_init(&hall), EDGE_OK);

    float angle = 0.0f;
    /* State 0 is the default table's illegal entry. */
    assert_int_equal(encoder_hall_update(&hall, 0u, &angle), EDGE_EINVAL);
    assert_int_equal(encoder_hall_update(NULL, 1u, &angle), EDGE_EINVAL);
    assert_int_equal(encoder_hall_update(&hall, 1u, NULL), EDGE_EINVAL);
    assert_int_equal(encoder_hall_update(&hall, 8u, &angle), EDGE_EINVAL);
    assert_int_equal(encoder_hall_update(&hall, 1u, &angle), EDGE_OK);

    /* A custom table is used as given, so what was illegal can become legal. */
    static const uint8_t custom[8] = {0u, 2u, 4u, 6u, 1u, 3u, 5u, 7u};
    encoder_hall_construct(&hall, custom);
    assert_int_equal(encoder_hall_update(&hall, 7u, &angle), EDGE_OK);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_encoder_as5047),
        cmocka_unit_test(test_encoder_mt6816),
        cmocka_unit_test(test_encoder_abi),
        cmocka_unit_test(test_encoder_sincos),
        cmocka_unit_test(test_encoder_hall),
        cmocka_unit_test(test_encoder_resolver_and_hall_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
