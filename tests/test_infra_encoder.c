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

/* A transfer that fails, for the paths where a dead bus must not read as an angle. */
static edge_status_t failing_spi(void *ctx, uint16_t tx_val, uint16_t *rx_val) {
    (void)ctx;
    (void)tx_val;
    *rx_val = 0u;
    return EDGE_EIO;
}

/*
 * The guards and the dead-bus paths of both SPI encoders, plus the guards of the resolvers. The
 * assertion is deliberately "not OK" rather than a specific code: what matters is that a bus that
 * cannot be read never reports an angle, and pinning the code would be pinning an implementation
 * detail this test has not read.
 */
static void test_encoder_spi_failure_and_guards(void **state) {
    (void)state;
    uint16_t raw = 0u;
    float angle = 0.0f;

    /* AS5047. */
    encoder_as5047_construct(NULL, mock_spi_transfer, NULL);
    assert_int_equal(encoder_as5047_init(NULL), EDGE_EINVAL);
    assert_true(encoder_as5047_read_angle_raw(NULL, &raw) != EDGE_OK);
    assert_true(encoder_as5047_read_angle_rad(NULL, &angle) != EDGE_OK);
    encoder_as5047_t as;
    encoder_as5047_construct(&as, NULL, NULL);
    assert_true(encoder_as5047_init(&as) != EDGE_OK);
    encoder_as5047_construct(&as, failing_spi, NULL);
    assert_true(encoder_as5047_read_angle_raw(&as, &raw) != EDGE_OK);
    assert_true(encoder_as5047_read_angle_rad(&as, &angle) != EDGE_OK);
    assert_true(encoder_as5047_read_diag(&as, &raw) != EDGE_OK);

    /* MT6816. */
    encoder_mt6816_construct(NULL, mock_spi_transfer, NULL);
    assert_int_equal(encoder_mt6816_init(NULL), EDGE_EINVAL);
    assert_true(encoder_mt6816_read_angle_raw(NULL, &raw) != EDGE_OK);
    assert_true(encoder_mt6816_read_angle_rad(NULL, &angle) != EDGE_OK);
    encoder_mt6816_t mt;
    encoder_mt6816_construct(&mt, failing_spi, NULL);
    /* Its init does not touch the bus, so a dead one still initialises; what must not happen is a
     * read reporting an angle. */
    assert_int_equal(encoder_mt6816_init(&mt), EDGE_OK);
    assert_true(encoder_mt6816_read_angle_raw(&mt, &raw) != EDGE_OK);
    assert_true(encoder_mt6816_read_angle_rad(&mt, &angle) != EDGE_OK);

    /* The resolver and the incremental encoder guard their own arguments. */
    assert_int_equal(encoder_abi_init(NULL), EDGE_EINVAL);
    encoder_abi_t abi;
    encoder_abi_construct(NULL, 4096u);
    encoder_abi_construct(&abi, 4096u);
    assert_int_equal(encoder_abi_init(&abi), EDGE_OK);
    assert_true(encoder_abi_update(NULL, 1, 0.01f, &angle) != EDGE_OK);
    assert_true(encoder_abi_update(&abi, 1, 0.01f, NULL) != EDGE_OK);
    assert_true(encoder_sincos_update(NULL, 0.0f, 1.0f, &angle) != EDGE_OK);
    encoder_sincos_t sc;
    encoder_sincos_construct(&sc, 0.0f, 0.0f, 1.0f, 1.0f);
    assert_int_equal(encoder_sincos_init(&sc), EDGE_OK);
    assert_true(encoder_sincos_update(&sc, 0.0f, 1.0f, NULL) != EDGE_OK);
}

/* A transfer whose response the test chooses, so the encoder's own checks can be driven. */
typedef struct scripted_spi {
    uint16_t response;
} scripted_spi_t;

static edge_status_t scripted_transfer(void *ctx, uint16_t tx_val, uint16_t *rx_val) {
    (void)tx_val;
    *rx_val = ((scripted_spi_t *)ctx)->response;
    return EDGE_OK;
}

/*
 * The checks the encoder makes on what comes back, and the two wraps on the way out. A response
 * whose parity is wrong, or that carries the error bit, must be refused rather than turned into an
 * angle - and a good response must go through the diagnostic read as well, which nothing called
 * with a working bus before.
 */
static void test_encoder_response_checks_and_wraps(void **state) {
    (void)state;
    scripted_spi_t spi;
    uint16_t raw = 0u;
    float angle = 0.0f;

    encoder_as5047_t as;
    encoder_as5047_construct(&as, scripted_transfer, &spi);

    /* A response with the wrong parity is refused. */
    spi.response = 0x0001u; /* one bit set, so the parity bit is missing */
    assert_true(encoder_as5047_read_angle_raw(&as, &raw) != EDGE_OK);

    /* A response with the error bit set is refused even with correct parity. */
    {
        uint16_t value = 0x4000u; /* error bit, no data */
        uint16_t bits = 0u;
        for (int i = 0; i < 14; i++) {
            if (value & (1u << i)) {
                bits++;
            }
        }
        if ((bits % 2) != 0) {
            value |= 0x8000u;
        }
        spi.response = value;
    }
    assert_true(encoder_as5047_read_angle_raw(&as, &raw) != EDGE_OK);

    /* A good response goes through the diagnostic read, which only ever ran on a dead bus. */
    spi.response = 0x1234u;
    assert_int_equal(encoder_as5047_read_diag(&as, &raw), EDGE_OK);
    assert_int_equal(raw, 0x1234u & 0x3FFFu);

    /* The incremental encoder wraps a negative accumulator rather than going negative. */
    encoder_abi_t abi;
    encoder_abi_construct(&abi, 4096u);
    assert_int_equal(encoder_abi_init(&abi), EDGE_OK);
    assert_int_equal(encoder_abi_update(&abi, -1, 0.01f, &angle), EDGE_OK);
    assert_true(angle >= 0.0f);
    assert_int_equal(encoder_abi_update(&abi, -8192, 0.01f, &angle), EDGE_OK);
    assert_true(angle >= 0.0f);

    /* The resolver wraps a negative angle into one turn as well. */
    encoder_sincos_t sc;
    encoder_sincos_construct(&sc, 0.0f, 0.0f, 1.0f, 1.0f);
    assert_int_equal(encoder_sincos_init(&sc), EDGE_OK);
    assert_int_equal(encoder_sincos_update(&sc, -0.5f, 0.5f, &angle), EDGE_OK);
    assert_true(angle >= 0.0f);
    assert_true(angle < 2.0f * (float)M_PI);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_encoder_as5047),
        cmocka_unit_test(test_encoder_mt6816),
        cmocka_unit_test(test_encoder_abi),
        cmocka_unit_test(test_encoder_sincos),
        cmocka_unit_test(test_encoder_hall),
        cmocka_unit_test(test_encoder_resolver_and_hall_guards),
        cmocka_unit_test(test_encoder_spi_failure_and_guards),
        cmocka_unit_test(test_encoder_response_checks_and_wraps),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
