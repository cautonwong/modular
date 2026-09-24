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
#include "encoder/encoder.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

typedef struct mock_encoder_ctx {
    uint16_t last_tx;
    uint16_t inject_rx;
    size_t transfer_count;
} mock_encoder_ctx_t;

static edge_status_t mock_encoder_spi_transfer(void *ctx, uint16_t tx_val, uint16_t *rx_val) {
    mock_encoder_ctx_t *m = (mock_encoder_ctx_t *)ctx;
    m->last_tx = tx_val;
    m->transfer_count++;
    *rx_val = m->inject_rx;
    return EDGE_OK;
}

static void test_as5047_parity_and_angle_read(void **state) {
    (void)state;
    mock_encoder_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    encoder_as5047_t enc;
    encoder_as5047_construct(&enc, mock_encoder_spi_transfer, &ctx);
    assert_int_equal(encoder_as5047_init(&enc), EDGE_OK);

    /* Angle = 8192 (half-turn = pi radians), no error bit */
    /* 8192 = 0x2000 (1 set bit). For even parity, bit 15 must be 1. */
    /* rx_val = 0x8000 | 0x2000 = 0xA000 */
    ctx.inject_rx = 0xA000u;
    assert_true(encoder_as5047_check_parity(ctx.inject_rx));

    float angle_rad = 0.0f;
    assert_int_equal(encoder_as5047_read_angle_rad(&enc, &angle_rad), EDGE_OK);
    assert_true(fabsf(angle_rad - (float)M_PI) < 1e-3f);

    /* Test parity error rejection */
    ctx.inject_rx = 0x2000u; /* Odd parity -> bad */
    assert_false(encoder_as5047_check_parity(ctx.inject_rx));
    assert_int_equal(encoder_as5047_read_angle_rad(&enc, &angle_rad), EDGE_EIO);
    assert_int_equal(enc.parity_errors, 1);
}

static void test_hall_decoder(void **state) {
    (void)state;
    encoder_hall_t hall;
    encoder_hall_construct(&hall, NULL);
    assert_int_equal(encoder_hall_init(&hall), EDGE_OK);

    float angle = 0.0f;
    /* Hall state 001 (step 0) -> angle 0.0 */
    assert_int_equal(encoder_hall_update(&hall, 1, &angle), EDGE_OK);
    assert_true(fabsf(angle - 0.0f) < 1e-4f);

    /* Hall state 011 (step 1) -> angle pi/3 */
    assert_int_equal(encoder_hall_update(&hall, 3, &angle), EDGE_OK);
    assert_true(fabsf(angle - ((float)M_PI / 3.0f)) < 1e-4f);

    /* Invalid hall state 000 -> EDGE_EINVAL */
    assert_int_equal(encoder_hall_update(&hall, 0, &angle), EDGE_EINVAL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_as5047_parity_and_angle_read),
        cmocka_unit_test(test_hall_decoder),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
