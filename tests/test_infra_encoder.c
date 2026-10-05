/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

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

/*
 * enc_sincos.c:44-101 transcribed, which is what the family's own version is held against. The
 * reference's helpers are inlined as they are there: the low-pass is the filtered value's error
 * subtracted from it (utils_math.h:100), and the angle is its own piecewise approximation in
 * radians (utils_math.c:193-215), made degrees and turned half a turn before it is normalised.
 */
typedef struct ref_sincos_state {
    uint32_t below_cnt;
    uint32_t above_cnt;
    float low_rate;
    float above_rate;
    float last_angle;
    float sin_f;
    float cos_f;
    float last_update_s;
} ref_sincos_state_t;

static float ref_sincos_fast_atan2(float y, float x) {
    float abs_y = fabsf(y) + 1e-20f;
    float angle;
    if (x >= 0.0f) {
        float r = (x - abs_y) / (x + abs_y);
        float rsq = r * r;
        angle = ((0.1963f * rsq) - 0.9817f) * r + (3.14159265358979323846f / 4.0f);
    } else {
        float r = (x + abs_y) / (abs_y - x);
        float rsq = r * r;
        angle = ((0.1963f * rsq) - 0.9817f) * r + (3.0f * 3.14159265358979323846f / 4.0f);
    }
    if (isnan(angle)) {
        angle = 0.0f;
    }
    return (y < 0.0f) ? -angle : angle;
}

static float ref_sincos_read_deg(ref_sincos_state_t *st, float s_gain, float c_gain, float s_off,
                                 float c_off, float k, float sph, float cph, float ratio,
                                 float delay_sign, float rpm, float now_s, float sin_v,
                                 float cos_v) {
    float sin = (sin_v - s_off) * s_gain;
    float cos = (cos_v - c_off) * c_gain;

    st->sin_f -= k * (st->sin_f - sin);
    st->cos_f -= k * (st->cos_f - cos);
    sin = st->sin_f;
    cos = st->cos_f;

    cos = (cos + sin * sph) / cph;

    float module = sin * sin + cos * cos;

    float timestep = now_s - st->last_update_s;
    if (timestep > 1.0f) {
        timestep = 1.0f;
    }
    st->last_update_s = now_s;

    if (module > (1.3f * 1.3f)) {
        ++st->above_cnt;
        st->above_rate -= timestep * (st->above_rate - 1.0f);
    } else if (module < (0.7f * 0.7f)) {
        ++st->below_cnt;
        st->low_rate -= timestep * (st->low_rate - 1.0f);
    } else {
        st->above_rate -= timestep * (st->above_rate - 0.0f);
        st->low_rate -= timestep * (st->low_rate - 0.0f);

        float delay_comp =
            ((1.0f - k) * (rpm * (float)((2.0 * 3.14159265358979323846) / 60.0)) * timestep) /
            (k * ratio);
        /* The reference converts the sum: the compensation is added while it is still radians. */
        float angle = (ref_sincos_fast_atan2(sin, cos) + delay_comp * delay_sign) *
                          (float)(180.0 / 3.14159265358979323846) +
                      180.0f;
        while (angle >= 180.0f) {
            angle -= 360.0f;
        }
        while (angle < -180.0f) {
            angle += 360.0f;
        }
        st->last_angle = angle;
    }

    return st->last_angle;
}

typedef struct mock_sincos_plant {
    float rpm;
    float now_s;
} mock_sincos_plant_t;

static float sincos_plant_rpm(void *self) {
    return ((mock_sincos_plant_t *)self)->rpm;
}

static float sincos_plant_now(void *self) {
    return ((mock_sincos_plant_t *)self)->now_s;
}

static void test_encoder_sincos_matches_the_reference(void **state) {
    (void)state;

    encoder_sincos_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.sin_gain = 1.05f;
    cfg.cos_gain = 0.95f;
    cfg.sin_offset = 1.65f;
    cfg.cos_offset = 1.70f;
    cfg.filter_constant = 0.2f;
    cfg.phase_correction_deg = 3.0f;
    cfg.sin_phase = sinf(3.0f * (float)(3.14159265358979323846 / 180.0));
    cfg.cos_phase = cosf(3.0f * (float)(3.14159265358979323846 / 180.0));
    cfg.ratio = 2.0f;
    cfg.delay_comp_sign = 1.0f;
    encoder_sincos_begin(&cfg);

    mock_sincos_plant_t plant = {.rpm = 1200.0f, .now_s = 0.0f};
    encoder_sincos_port_t port = {
        .self = &plant, .read_rpm = sincos_plant_rpm, .now_seconds = sincos_plant_now};

    ref_sincos_state_t ref;
    memset(&ref, 0, sizeof(ref));

    /* A swept pair of readings, with the speed and the clock moving as they would on a machine. */
    for (int i = 0; i < 600; i++) {
        const float t = (float)i * 0.0005f;
        const float amplitude = 1.0f + 0.02f * sinf((float)i * 0.05f);
        const float sin_v = 1.65f + amplitude * sinf(t * 20.0f);
        const float cos_v = 1.70f + amplitude * cosf(t * 20.0f);
        plant.rpm = 1200.0f + 200.0f * sinf((float)i * 0.02f);
        plant.now_s = t;

        const float mine = encoder_sincos_read_deg(&cfg, &port, sin_v, cos_v);
        const float theirs =
            ref_sincos_read_deg(&ref, cfg.sin_gain, cfg.cos_gain, cfg.sin_offset, cfg.cos_offset,
                                cfg.filter_constant, cfg.sin_phase, cfg.cos_phase, cfg.ratio,
                                cfg.delay_comp_sign, plant.rpm, plant.now_s, sin_v, cos_v);
        assert_float_equal(mine, theirs, 1e-4f);
    }

    assert_int_equal(cfg.state.signal_above_max_error_cnt, ref.above_cnt);
    assert_int_equal(cfg.state.signal_below_min_error_cnt, ref.below_cnt);
    assert_float_equal(cfg.state.signal_above_max_error_rate, ref.above_rate, 1e-5f);
    assert_float_equal(cfg.state.signal_low_error_rate, ref.low_rate, 1e-5f);

    /*
     * Outside the window the measurement is discarded: the error counts rise, the rates climb
     * towards one, and the angle stays where the last good reading left it. The filter is opened
     * here so that one reading can leave the window on its own, which is what makes this the
     * reference's own case rather than the filter's.
     */
    cfg.filter_constant = 1.0f;
    plant.now_s += 0.001f;
    const float huge = encoder_sincos_read_deg(&cfg, &port, 1.65f + 3.0f, 1.70f + 3.0f);
    const float ref_huge = ref_sincos_read_deg(&ref, cfg.sin_gain, cfg.cos_gain, cfg.sin_offset,
                                               cfg.cos_offset, cfg.filter_constant, cfg.sin_phase,
                                               cfg.cos_phase, cfg.ratio, cfg.delay_comp_sign,
                                               plant.rpm, plant.now_s, 1.65f + 3.0f, 1.70f + 3.0f);
    assert_float_equal(huge, ref_huge, 1e-5f);
    assert_int_equal(cfg.state.signal_above_max_error_cnt, ref.above_cnt);
    assert_float_equal(cfg.state.signal_above_max_error_rate, ref.above_rate, 1e-5f);

    plant.now_s += 0.001f;
    const float tiny = encoder_sincos_read_deg(&cfg, &port, 1.65f, 1.70f);
    const float ref_tiny =
        ref_sincos_read_deg(&ref, cfg.sin_gain, cfg.cos_gain, cfg.sin_offset, cfg.cos_offset,
                            cfg.filter_constant, cfg.sin_phase, cfg.cos_phase, cfg.ratio,
                            cfg.delay_comp_sign, plant.rpm, plant.now_s, 1.65f, 1.70f);
    assert_float_equal(tiny, ref_tiny, 1e-5f);
    assert_int_equal(cfg.state.signal_below_min_error_cnt, ref.below_cnt);
    assert_float_equal(cfg.state.signal_low_error_rate, ref.low_rate, 1e-5f);

    /* Both readings were discarded, so both report where the last good one left the angle. */
    assert_float_equal(huge, tiny, 1e-6f);

    /* Nothing to read through. */
    assert_float_equal(encoder_sincos_read_deg(&cfg, NULL, 1.0f, 1.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_sincos_read_deg(NULL, &port, 1.0f, 1.0f), 0.0f, 1e-6f);
    encoder_sincos_begin(NULL);
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

    encoder_sincos_config_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.sin_gain = 0.5f;
    sc.cos_gain = 0.25f;
    encoder_sincos_begin(&sc);
    assert_float_equal(sc.state.sin_filter, 0.0f, 1e-6f);
    assert_float_equal(sc.state.cos_filter, 0.0f, 1e-6f);
    assert_float_equal(sc.state.last_enc_angle, 0.0f, 1e-6f);
    assert_int_equal(sc.state.signal_above_max_error_cnt, 0u);
    encoder_sincos_begin(NULL);

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
    /*
     * The resolver's own pair of guards, and the arithmetic a configuration with nothing in it
     * makes: its cosine of the phase correction is nought, so the cosine it takes is divided by it
     * - which is the reference's own arithmetic rather than a case it guards against.
     */
    encoder_sincos_config_t sc;
    memset(&sc, 0, sizeof(sc));
    mock_sincos_plant_t plant = {0};
    encoder_sincos_port_t port = {
        .self = &plant, .read_rpm = sincos_plant_rpm, .now_seconds = sincos_plant_now};
    assert_float_equal(encoder_sincos_read_deg(&sc, NULL, 0.0f, 1.0f), 0.0f, 1e-6f);
    assert_true(isnan(encoder_sincos_read_deg(&sc, &port, 0.0f, 1.0f)));
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

    /*
     * The resolver's range is the reference's: degrees, half a turn either way, from a filter that
     * passes what it is given with no phase correction to make.
     */
    encoder_sincos_config_t sc;
    memset(&sc, 0, sizeof(sc));
    sc.sin_gain = 1.0f;
    sc.cos_gain = 1.0f;
    sc.filter_constant = 1.0f;
    sc.cos_phase = 1.0f;
    sc.ratio = 1.0f;
    encoder_sincos_begin(&sc);
    mock_sincos_plant_t plant = {.rpm = 0.0f, .now_s = 0.0f};
    encoder_sincos_port_t port = {
        .self = &plant, .read_rpm = sincos_plant_rpm, .now_seconds = sincos_plant_now};
    const float deg = encoder_sincos_read_deg(&sc, &port, -0.5f, 0.5f);
    assert_true(deg >= -180.0f);
    assert_true(deg < 180.0f);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_encoder_as5047),
        cmocka_unit_test(test_encoder_mt6816),
        cmocka_unit_test(test_encoder_abi),
        cmocka_unit_test(test_encoder_sincos_matches_the_reference),
        cmocka_unit_test(test_encoder_hall),
        cmocka_unit_test(test_encoder_resolver_and_hall_guards),
        cmocka_unit_test(test_encoder_spi_failure_and_guards),
        cmocka_unit_test(test_encoder_response_checks_and_wraps),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
