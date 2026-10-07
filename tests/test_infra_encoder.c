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

/*
 * A plant that answers the family's frames. The reference's bus is bit-banged and its answers
 * arrive one exchange late, which is what this mock does: it hands back the frame it was given last
 * time.
 */
typedef struct mock_as504x_plant {
    uint16_t last_pos;
    bool fail_parity;
    int transfers;
} mock_as504x_plant_t;

static void as504x_plant_transfer(void *self, uint16_t *in_buf, const uint16_t *out_buf,
                                  int length) {
    mock_as504x_plant_t *p = (mock_as504x_plant_t *)self;
    for (int i = 0; i < length; i++) {
        if (in_buf != NULL) {
            in_buf[i] = p->last_pos;
        }
        if (out_buf != NULL) {
            p->last_pos = out_buf[i];
        }
        p->transfers++;
    }
}

/* A word whose parity is whole, which is what the routine's own test asks of it. */
static uint16_t as504x_word_with_parity(uint16_t value) {
    uint16_t word = (uint16_t)(value & 0x3FFFu);
    if (!encoder_as504x_parity_ok(word)) {
        word |= 0x8000u;
    }
    return word;
}

/*
 * enc_as504x.c:80-148's routine on the path without a MOSI line, which is the one whose every word
 * is checked for parity and whose angle is the low fourteen bits over a quarter turn of turns - so
 * four thousand and ninety-six counts is a right angle - with a word of nothing but noughts or ones
 * counted until the connection is given up on (:133-148).
 */
static void test_encoder_as504x_matches_the_reference(void **state) {
    (void)state;

    encoder_as504x_state_t as;
    encoder_as504x_begin(&as);
    mock_as504x_plant_t as_plant = {0};
    encoder_as504x_port_t as_port = {
        .self = &as_plant, .has_mosi = false, .transfer16 = as504x_plant_transfer};

    /* The port answers with the frame before the one it is given, so the first read is primed. */
    const uint16_t good = as504x_word_with_parity(4096u);
    as_plant.last_pos = good;
    assert_float_equal(encoder_as504x_routine(&as, &as_port, 0.001f), 90.0f, 1e-3f);
    assert_int_equal(as.spi_val, good);
    assert_int_equal(as.spi_error_cnt, 0u);
    assert_true(as.sensor_diag.is_connected);

    /* A word whose parity does not hold raises the count and leaves the angle where it was. */
    const float held = as.last_enc_angle;
    as_plant.last_pos = 0x0001u; /* one bit of data, which is an odd count */
    encoder_as504x_routine(&as, &as_port, 0.002f);
    assert_true(as.spi_error_cnt >= 1u);
    assert_float_equal(as.last_enc_angle, held, 1e-6f);

    /* A word of nothing but noughts is counted, and enough of them lose the connection. */
    as_plant.last_pos = 0x0000u;
    for (uint32_t i = 0; i < AS504X_DATA_INVALID_THRESHOLD + 5u; i++) {
        encoder_as504x_routine(&as, &as_port, 0.003f + 0.0001f * (float)i);
    }
    assert_int_equal(as.sensor_diag.is_connected, 0u);
    assert_int_equal(as.data_last_invalid_counter, AS504X_DATA_INVALID_THRESHOLD);

    encoder_as504x_begin(NULL);
    assert_float_equal(encoder_as504x_routine(NULL, &as_port, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_as504x_routine(&as, NULL, 0.0f), 0.0f, 1e-6f);
    encoder_as504x_port_t bare = {0};
    assert_float_equal(encoder_as504x_routine(&as, &bare, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_as504x_read_angle(&as, &as_port, 0.0f), as.last_enc_angle, 1e-6f);
}

typedef struct mock_mt6816_plant {
    uint16_t reg03;
    uint16_t reg04;
    bool fail;
} mock_mt6816_plant_t;

static edge_status_t mt6816_plant_read(void *self, uint16_t *reg03, uint16_t *reg04) {
    mock_mt6816_plant_t *p = (mock_mt6816_plant_t *)self;
    if (p->fail) {
        return EDGE_ESTATE;
    }
    *reg03 = p->reg03;
    *reg04 = p->reg04;
    return EDGE_OK;
}

/*
 * enc_mt6816.c:40-106's routine and driver/spi_bb.c:309-316's parity, which is what the family is
 * held against: one word made of the sensor's two registers, that word's odd parity, the second bit
 * saying whether the magnet is where it should be, and the low fourteen over a quarter turn of
 * turns.
 */
static void test_encoder_mt6816_matches_the_reference(void **state) {
    (void)state;

    /*
     * The fold's low bit is what decides: it is set when the bits come to one, so a word whose
     * count is even passes and one whose count is odd does not - which is what the parity bit these
     * words carry on top is there to make true.
     */
    assert_true(encoder_mt6816_parity_ok(0x0000u));
    assert_true(encoder_mt6816_parity_ok(0x0003u));
    assert_false(encoder_mt6816_parity_ok(0x0001u));
    assert_false(encoder_mt6816_parity_ok(0x8000u));

    encoder_mt6816_state_t st;
    encoder_mt6816_begin(&st);
    mock_mt6816_plant_t plant = {0};
    encoder_mt6816_port_t port = {.self = &plant, .read_registers = mt6816_plant_read};

    /* A whole word of four thousand counts, with the parity bit set so that it holds. */
    uint16_t word = (uint16_t)(4000u << 2);
    if (!encoder_mt6816_parity_ok(word)) {
        word |= 0x8000u;
    }
    plant.reg03 = (uint16_t)(word >> 8);
    plant.reg04 = (uint16_t)(word & 0xFFu);
    assert_float_equal(encoder_mt6816_routine(&st, &port, 0.001f), 4000.0f * 360.0f / 16384.0f,
                       1e-4f);
    assert_int_equal(st.spi_error_cnt, 0u);
    assert_int_equal(st.no_magnet_error_cnt, 0u);
    assert_int_equal(st.spi_val, word);

    /*
     * The magnet's own bit raises its count and its rate, and the angle holds where it was. The
     * parity bit goes on top so that the word is one that passes: two bits, which is even.
     */
    const float held = st.last_enc_angle;
    word = 0x8002u;
    plant.reg03 = (uint16_t)(word >> 8);
    plant.reg04 = (uint16_t)(word & 0xFFu);
    assert_true(encoder_mt6816_parity_ok(word));
    encoder_mt6816_routine(&st, &port, 0.002f);
    assert_int_equal(st.no_magnet_error_cnt, 1u);
    assert_float_equal(st.no_magnet_error_rate, 0.001f, 1e-6f);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* A word whose parity fails raises the bus count instead: one bit is an odd count. */
    word = 0x0001u;
    plant.reg03 = 0u;
    plant.reg04 = (uint16_t)(word & 0xFFu);
    assert_false(encoder_mt6816_parity_ok(word));
    encoder_mt6816_routine(&st, &port, 0.003f);
    assert_int_equal(st.spi_error_cnt, 1u);
    assert_float_equal(st.spi_error_rate, 0.001f, 1e-6f);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* A bus the port could not read leaves everything as it was. */
    plant.fail = true;
    assert_float_equal(encoder_mt6816_routine(&st, &port, 0.004f), held, 1e-6f);

    encoder_mt6816_begin(NULL);
    assert_float_equal(encoder_mt6816_routine(NULL, &port, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_mt6816_routine(&st, NULL, 0.0f), 0.0f, 1e-6f);
    encoder_mt6816_port_t bare = {0};
    assert_float_equal(encoder_mt6816_routine(&st, &bare, 0.0f), 0.0f, 1e-6f);
}

/*
 * A plant whose counter and index pin the test drives, so the machine's own branches can be walked.
 */
typedef struct mock_abi_plant {
    uint32_t count;
    bool index_high;
    int writes;
    uint32_t last_written;
} mock_abi_plant_t;

static uint32_t abi_plant_read_count(void *self) {
    return ((mock_abi_plant_t *)self)->count;
}

static void abi_plant_write_count(void *self, uint32_t count) {
    mock_abi_plant_t *p = (mock_abi_plant_t *)self;
    p->writes++;
    p->last_written = count;
    p->count = count;
}

static bool abi_plant_index_high(void *self) {
    return ((mock_abi_plant_t *)self)->index_high;
}

/*
 * enc_abi.c:100-124, the index pulse's machine, branch by branch. Every assertion here is one of
 * the reference's own steps: the pin's level once it has settled, the counter remembered and
 * counted, the plausibility window of a twentieth of a revolution either side of the wrap, and the
 * five bad pulses in a row that lose the index again.
 */
static void test_encoder_abi_index_machine(void **state) {
    (void)state;

    encoder_abi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.counts = 4096u;
    encoder_abi_begin(&cfg);
    assert_false(cfg.state.index_found);
    assert_int_equal(cfg.state.index_pulse_cnt, 0u);

    mock_abi_plant_t plant = {0};
    encoder_abi_port_t port = {.self = &plant,
                               .read_count = abi_plant_read_count,
                               .write_count = abi_plant_write_count,
                               .read_index_high = abi_plant_index_high};

    /* A pulse whose pin has already fallen is not the index. */
    plant.index_high = false;
    plant.count = 123u;
    encoder_abi_index_pulse(&cfg, &port);
    assert_int_equal(cfg.state.index_pulse_cnt, 0u);
    assert_int_equal(plant.writes, 0);

    /* The first one that holds is taken: the counter starts again from where it is. */
    plant.index_high = true;
    encoder_abi_index_pulse(&cfg, &port);
    assert_true(cfg.state.index_found);
    assert_int_equal(cfg.state.cnt_at_ind_last, 123u);
    assert_int_equal(cfg.state.index_pulse_cnt, 1u);
    assert_int_equal(cfg.state.bad_pulses, 0);
    assert_int_equal(plant.writes, 1);
    assert_int_equal(plant.last_written, 0u);

    /* One at the very end of a revolution is where a revolution begins: plausible, so it resets. */
    plant.count = 4095u;
    encoder_abi_index_pulse(&cfg, &port);
    assert_true(cfg.state.index_found);
    assert_int_equal(cfg.state.bad_pulses, 0);
    assert_int_equal(plant.writes, 2);

    /* One in the middle of a revolution is not, and five of those in a row are too many. */
    plant.count = 2048u;
    for (int i = 0; i < 5; i++) {
        encoder_abi_index_pulse(&cfg, &port);
        assert_true(cfg.state.index_found);
        assert_int_equal(cfg.state.bad_pulses, i + 1);
    }
    assert_int_equal(plant.writes, 2);
    encoder_abi_index_pulse(&cfg, &port);
    assert_int_equal(cfg.state.bad_pulses, 6);
    assert_false(cfg.state.index_found);

    /* With the index lost, the next one that holds is taken again. */
    encoder_abi_index_pulse(&cfg, &port);
    assert_true(cfg.state.index_found);
    assert_int_equal(cfg.state.bad_pulses, 0);
    assert_int_equal(plant.writes, 3);

    /* And the reading is the counter over one revolution, in degrees. */
    assert_float_equal(encoder_abi_read_deg(0u, 4096u), 0.0f, 1e-6f);
    assert_float_equal(encoder_abi_read_deg(1024u, 4096u), 90.0f, 1e-4f);
    assert_float_equal(encoder_abi_read_deg(4095u, 4096u), 359.91211f, 1e-3f);
    assert_float_equal(encoder_abi_read_deg(100u, 0u), 0.0f, 1e-6f);

    /* Nothing to read through or write into. */
    encoder_abi_index_pulse(&cfg, NULL);
    encoder_abi_index_pulse(NULL, &port);
    encoder_abi_port_t bare = {0};
    encoder_abi_index_pulse(&cfg, &bare);
    encoder_abi_begin(NULL);
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
}

/* A transfer that fails, for the paths where a dead bus must not read as an angle. */
/*
 * The guards and the dead-bus paths of both SPI encoders, plus the guards of the resolvers. The
 * assertion is deliberately "not OK" rather than a specific code: what matters is that a bus that
 * cannot be read never reports an angle, and pinning the code would be pinning an implementation
 * detail this test has not read.
 */
static void test_encoder_spi_failure_and_guards(void **state) {
    (void)state;

    /* AS504x: no state, no port, and a word of nothing that loses the connection. */
    encoder_as504x_begin(NULL);
    encoder_as504x_state_t as;
    encoder_as504x_begin(&as);
    assert_int_equal(as.spi_error_cnt, 0u);
    assert_false(as.sensor_diag.is_connected);
    assert_float_equal(encoder_as504x_routine(&as, NULL, 0.0f), 0.0f, 1e-6f);
    encoder_as504x_port_t bare_as = {0};
    assert_float_equal(encoder_as504x_routine(&as, &bare_as, 0.0f), 0.0f, 1e-6f);

    /* MT6816: no configuration, no port. */
    encoder_mt6816_begin(NULL);
    encoder_mt6816_state_t mt;
    encoder_mt6816_begin(&mt);
    assert_int_equal(mt.spi_error_cnt, 0u);
    assert_float_equal(encoder_mt6816_routine(&mt, NULL, 0.0f), 0.0f, 1e-6f);
    encoder_mt6816_port_t bare_mt = {0};
    assert_float_equal(encoder_mt6816_routine(&mt, &bare_mt, 0.0f), 0.0f, 1e-6f);

    /* The incremental encoder's own machine: no configuration, no port, nothing happens. */
    encoder_abi_config_t abi;
    memset(&abi, 0, sizeof(abi));
    encoder_abi_port_t abi_port = {0};
    encoder_abi_begin(NULL);
    encoder_abi_begin(&abi);
    assert_int_equal(abi.state.index_pulse_cnt, 0u);
    assert_false(abi.state.index_found);
    encoder_abi_index_pulse(NULL, NULL);
    encoder_abi_index_pulse(&abi, NULL);
    encoder_abi_index_pulse(NULL, &abi_port);
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

/*
 * The checks the encoder makes on what comes back, and the two wraps on the way out. A response
 * whose parity is wrong, or that carries the error bit, must be refused rather than turned into an
 * angle - and a good response must go through the diagnostic read as well, which nothing called
 * with a working bus before.
 */
static void test_encoder_response_checks_and_wraps(void **state) {
    (void)state;

    /*
     * The AS504x's checks are the routine's own. Without a MOSI line the word's parity is all there
     * is: one bit is an odd count, so it is not an angle, and a whole word is the low fourteen bits
     * over a quarter turn. With one, the error bit is what refuses a word, and the angle the state
     * already had is what comes back.
     */
    encoder_as504x_state_t as;
    encoder_as504x_begin(&as);
    mock_as504x_plant_t as_plant = {0};
    encoder_as504x_port_t as_port = {
        .self = &as_plant, .has_mosi = false, .transfer16 = as504x_plant_transfer};

    as_plant.last_pos = 0x0001u;
    encoder_as504x_routine(&as, &as_port, 0.001f);
    assert_int_equal(as.spi_error_cnt, 1u);
    assert_float_equal(as.last_enc_angle, 0.0f, 1e-6f);

    as_plant.last_pos = as504x_word_with_parity(0x1234u);
    assert_float_equal(encoder_as504x_routine(&as, &as_port, 0.002f),
                       (float)0x1234u * 360.0f / 16384.0f, 1e-3f);

    encoder_as504x_state_t err;
    encoder_as504x_begin(&err);
    mock_as504x_plant_t mosi_plant = {0};
    encoder_as504x_port_t mosi_port = {
        .self = &mosi_plant, .has_mosi = true, .transfer16 = as504x_plant_transfer};
    mosi_plant.last_pos = (uint16_t)(as504x_word_with_parity(0x1234u) | 0x4000u);
    encoder_as504x_routine(&err, &mosi_port, 0.001f);
    assert_int_equal(err.spi_data_err_raised, 1u);
    assert_float_equal(err.last_enc_angle, 0.0f, 1e-6f);

    /* The counter's own reading, over one revolution and in degrees. */
    assert_float_equal(encoder_abi_read_deg(0u, 4096u), 0.0f, 1e-6f);
    assert_float_equal(encoder_abi_read_deg(1024u, 4096u), 90.0f, 1e-4f);

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

/*
 * enc_pwm.c:36-63's callback and :55-62's reading, which is what the family is held against: the
 * angle the shorter of the width and the period makes, the shortest way round between two updates
 * for the speed, and the interpolation that speed drives, clamped to a third of a turn.
 */
static void test_encoder_pwm_matches_the_reference(void **state) {
    (void)state;

    encoder_pwm_state_t st;
    encoder_pwm_begin(&st, true, false);
    assert_int_equal(encoder_pwm_update_count(&st), 0u);

    /* A quarter of a period wide is a quarter turn, and the update is ABI's too. */
    bool write_abi = false;
    uint32_t abi_count = 0u;
    encoder_pwm_update(&st, 250u, 1000u, 0.001f, 4096u, &write_abi, &abi_count);
    assert_float_equal(st.angle, 90.0f, 1e-4f);
    assert_int_equal(encoder_pwm_update_count(&st), 1u);
    assert_true(write_abi);
    assert_int_equal(abi_count, 1024u);
    /* The first two updates only establish the speed. */
    assert_float_equal(st.speed_per_s, 0.0f, 1e-6f);

    /* A capture wider than its own period is clamped to it, which is a whole turn. */
    encoder_pwm_update(&st, 2000u, 1000u, 0.002f, 4096u, &write_abi, &abi_count);
    assert_float_equal(st.angle, 360.0f, 1e-4f);

    /* The third establishes it: from a whole turn to half of one, over a millisecond. */
    encoder_pwm_update(&st, 500u, 1000u, 0.003f, 4096u, &write_abi, &abi_count);
    assert_float_equal(st.angle, 180.0f, 1e-4f);
    assert_float_equal(st.speed_per_s, -180.0f / 0.001f, 1.0f);

    /* And the reading interpolates on it: a millisecond of it is already past the clamp. */
    assert_float_equal(encoder_pwm_read_deg(&st, 0.003f), 180.0f, 1e-3f);
    assert_float_equal(encoder_pwm_read_deg(&st, 0.004f), 60.0f, 1e-2f);
    /* Ten seconds later the clamp is still what holds it: half a turn less a third of one. */
    assert_float_equal(encoder_pwm_read_deg(&st, 10.0f), 60.0f, 1e-2f);

    /* Inverted, a quarter of a period is three quarters of a turn. */
    encoder_pwm_set_inverted(&st, true);
    encoder_pwm_update(&st, 250u, 1000u, 0.005f, 4096u, &write_abi, &abi_count);
    assert_float_equal(st.angle, 270.0f, 1e-4f);

    /* Not writing the ABI timer, and nothing to write into. */
    encoder_pwm_begin(&st, false, false);
    encoder_pwm_update(&st, 250u, 1000u, 1.0f, 4096u, &write_abi, &abi_count);
    assert_false(write_abi);
    encoder_pwm_update(&st, 250u, 1000u, 1.0f, 4096u, NULL, NULL);

    /* A capture that reports no period leaves the angle where it was. */
    const float held = st.angle;
    encoder_pwm_update(&st, 250u, 0u, 2.0f, 4096u, NULL, NULL);
    assert_float_equal(st.angle, held, 1e-6f);

    encoder_pwm_update(NULL, 1u, 2u, 0.0f, 1u, NULL, NULL);
    encoder_pwm_set_inverted(NULL, true);
    encoder_pwm_begin(NULL, true, true);
    assert_float_equal(encoder_pwm_read_deg(NULL, 0.0f), 0.0f, 1e-6f);
    assert_int_equal(encoder_pwm_update_count(NULL), 0u);
}

typedef struct mock_amt22_plant {
    uint16_t word;
    bool fail;
} mock_amt22_plant_t;

static edge_status_t amt22_plant_read(void *self, uint16_t *word) {
    mock_amt22_plant_t *p = (mock_amt22_plant_t *)self;
    if (p->fail) {
        return EDGE_ESTATE;
    }
    *word = p->word;
    return EDGE_OK;
}

/*
 * enc_amt22.c:79-89's checksum and :40-72's routine. The checksum seeds itself with both bits and
 * folds the pairs in, so a word whose pairs come to nought needs those two bits clear (0x0003), and
 * one whose pairs come to both needs them set (0xC000); 0x0000 leaves the seed against a clear pair
 * of bits and is the failing case.
 */
static void test_encoder_amt22_matches_the_reference(void **state) {
    (void)state;

    assert_true(encoder_amt22_checksum_ok(0xC000u));
    assert_true(encoder_amt22_checksum_ok(0x0003u));
    assert_false(encoder_amt22_checksum_ok(0x0000u));
    assert_false(encoder_amt22_checksum_ok(0x4000u));
    /* The top of the range carries the seed's complement: the seven pairs come to both. */
    assert_true(encoder_amt22_checksum_ok(0x3FFFu));

    encoder_amt22_state_t st;
    encoder_amt22_begin(&st);
    assert_float_equal(st.last_enc_angle, 0.0f, 1e-6f);

    mock_amt22_plant_t plant = {0};
    encoder_amt22_port_t port = {.self = &plant, .read_word = amt22_plant_read};

    /* A word whose checksum holds is the angle its low fourteen bits make. */
    uint16_t word = (uint16_t)(1000u & 0x3FFFu);
    uint16_t pairs = 0u;
    for (int i = 0; i < 14; i += 2) {
        pairs ^= (uint16_t)((word >> i) & 0x3u);
    }
    plant.word = (uint16_t)(word | (uint16_t)((0x3u ^ pairs) << 14));
    assert_true(encoder_amt22_checksum_ok(plant.word));
    assert_float_equal(encoder_amt22_routine(&st, &port, 0.001f), 1000.0f * 360.0f / 16384.0f,
                       1e-4f);
    assert_int_equal(st.spi_error_cnt, 0u);
    /* The error rate falls from wherever it was towards nought, by the timestep. */
    assert_float_equal(st.spi_error_rate, 0.0f, 1e-6f);

    /* A word whose checksum does not is counted and moves the rate towards one. */
    plant.word = 0x0000u;
    encoder_amt22_routine(&st, &port, 0.002f);
    assert_int_equal(st.spi_error_cnt, 1u);
    assert_float_equal(st.spi_error_rate, 0.001f, 1e-6f);
    /* And the angle is the last good one rather than the failed word's. */
    assert_float_equal(st.last_enc_angle, 1000.0f * 360.0f / 16384.0f, 1e-4f);

    /* A word the port could not read leaves everything as it was. */
    plant.fail = true;
    const float held = st.last_enc_angle;
    assert_float_equal(encoder_amt22_routine(&st, &port, 0.003f), held, 1e-6f);

    encoder_amt22_begin(NULL);
    assert_float_equal(encoder_amt22_routine(NULL, &port, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_amt22_routine(&st, NULL, 0.0f), 0.0f, 1e-6f);
    encoder_amt22_port_t bare = {0};
    assert_float_equal(encoder_amt22_routine(&st, &bare, 0.0f), 0.0f, 1e-6f);
}

typedef struct mock_mt6835_plant {
    uint8_t rx[6];
    bool fail;
} mock_mt6835_plant_t;

static edge_status_t mt6835_plant_burst(void *self, uint8_t rx[6]) {
    mock_mt6835_plant_t *p = (mock_mt6835_plant_t *)self;
    if (p->fail) {
        return EDGE_ESTATE;
    }
    for (int i = 0; i < 6; i++) {
        rx[i] = p->rx[i];
    }
    return EDGE_OK;
}

/*
 * enc_mt6835.c:60-75's CRC-8, whose check value over the digits one to nine is the ordinary one for
 * this polynomial, and :89-129's routine: a twenty-one bit angle over two to the twenty-one, with
 * the CRC over the three bytes and the sensor's own two status bits both having to hold.
 */
static void test_encoder_mt6835_matches_the_reference(void **state) {
    (void)state;

    const uint8_t digits[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    assert_int_equal(encoder_mt6835_crc8(digits, 9), 0xF4u);
    assert_int_equal(encoder_mt6835_crc8(digits, 0), 0x00u);

    encoder_mt6835_state_t st;
    encoder_mt6835_begin(&st);
    mock_mt6835_plant_t plant = {0};
    encoder_mt6835_port_t port = {.self = &plant, .read_burst = mt6835_plant_burst};

    /* A whole burst: the twenty-one bits spread over three bytes, their CRC, and no status. */
    const uint32_t angle_bits = 1000000u;
    plant.rx[0] = 0xA0u; /* MT6835_BURST_CMD */
    plant.rx[1] = 0x03u; /* MT6835_BURST_ADDR */
    plant.rx[2] = (uint8_t)(angle_bits >> 13);
    plant.rx[3] = (uint8_t)(angle_bits >> 5);
    plant.rx[4] = (uint8_t)((angle_bits << 3) & 0xF8u);
    plant.rx[5] = encoder_mt6835_crc8(&plant.rx[2], 3);
    assert_float_equal(encoder_mt6835_routine(&st, &port, 0.001f), 1000000.0f * 360.0f / 2097152.0f,
                       1e-3f);
    assert_int_equal(st.spi_error_cnt, 0u);
    assert_int_equal(st.spi_val, angle_bits);

    /* A CRC that does not hold raises the bus count and leaves the angle where it was. */
    const float held = st.last_enc_angle;
    plant.rx[5] ^= 0x01u;
    encoder_mt6835_routine(&st, &port, 0.002f);
    assert_int_equal(st.spi_error_cnt, 1u);
    assert_float_equal(st.spi_error_rate, 0.001f, 1e-6f);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* So does a status that is not clear, even with the CRC right. */
    plant.rx[4] |= 0x01u;
    plant.rx[5] = encoder_mt6835_crc8(&plant.rx[2], 3);
    encoder_mt6835_routine(&st, &port, 0.003f);
    assert_int_equal(st.spi_error_cnt, 2u);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* A burst the port could not read leaves everything as it was. */
    plant.fail = true;
    assert_float_equal(encoder_mt6835_routine(&st, &port, 0.004f), held, 1e-6f);

    encoder_mt6835_begin(NULL);
    assert_float_equal(encoder_mt6835_routine(NULL, &port, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_mt6835_routine(&st, NULL, 0.0f), 0.0f, 1e-6f);
    encoder_mt6835_port_t bare = {0};
    assert_float_equal(encoder_mt6835_routine(&st, &bare, 0.0f), 0.0f, 1e-6f);
}

/*
 * enc_bissc.c:64-80's table and :124-180's frame. The table is held against the polynomial folded
 * by hand in the test, so that the frame's own checksum is verified against something other than
 * itself; the frame is a thirty-two bit one - a start bit, the CDS bit, twenty-two position bits,
 * the error and warning bits and six checksum bits - laid out as the sensor sends it.
 */
static uint8_t ref_crc6_entry(int i) {
    int crc = i;
    for (int j = 0; j < 6; j++) {
        if ((crc & 0x20) != 0) {
            crc = (crc << 1) ^ 0x43;
        } else {
            crc = crc << 1;
        }
    }
    return (uint8_t)crc;
}

static void test_encoder_bissc_matches_the_reference(void **state) {
    (void)state;

    encoder_bissc_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    encoder_bissc_begin(&cfg, 22u);
    assert_int_equal(cfg.enc_res, 22u);
    for (int i = 0; i < 64; i++) {
        assert_int_equal(cfg.table_crc6n[i], ref_crc6_entry(i));
    }

    /* The frame the sensor would send for a position of a thousand, with both flags clear. */
    const uint32_t position = 1000u;
    const uint32_t data_rx = position << 2; /* error and warning are the two bits below it */
    const uint8_t crc = encoder_bissc_crc6(cfg.table_crc6n, data_rx);
    const uint32_t word = (1u << 31) | (data_rx << 6) | crc;

    /*
     * The eight bytes the decoder is given are a shift register's word: the frame's own bits sit at
     * the bottom of it and the bit that started the read is at the top, which is what makes the
     * trim inside the decode line the frame up with the checksum at its low end.
     */
    const uint64_t word64 = (1ull << 63) | (uint64_t)word;
    uint8_t frame[8] = {0};
    for (int i = 0; i < 8; i++) {
        frame[i] = (uint8_t)(word64 >> (56 - 8 * i));
    }

    assert_float_equal(encoder_bissc_frame(&cfg, frame, 0.001f),
                       1000.0f * 360.0f / (float)((1u << 22) - 1u), 1e-3f);
    assert_int_equal(cfg.state.spi_val, position);
    assert_int_equal(cfg.state.spi_data_error_cnt, 0u);

    /* A checksum that does not match raises the count and leaves the angle where it was. */
    const float held = cfg.state.last_enc_angle;
    frame[7] ^= 0x01u;
    encoder_bissc_frame(&cfg, frame, 0.002f);
    assert_int_equal(cfg.state.spi_data_error_cnt, 1u);
    assert_float_equal(cfg.state.spi_data_error_rate, 0.001f, 1e-6f);
    assert_float_equal(cfg.state.last_enc_angle, held, 1e-6f);

    /* A frame of nothing at all is the case the reference leaves undefined; it counts as bad. */
    uint8_t empty[8] = {0};
    encoder_bissc_frame(&cfg, empty, 0.003f);
    assert_int_equal(cfg.state.spi_data_error_cnt, 2u);
    assert_float_equal(cfg.state.last_enc_angle, held, 1e-6f);

    encoder_bissc_begin(NULL, 22u);
    assert_float_equal(encoder_bissc_frame(NULL, frame, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_bissc_frame(&cfg, NULL, 0.0f), 0.0f, 1e-6f);
    assert_int_equal(encoder_bissc_crc6(NULL, 0u), 0u);
    /* A resolution outside what the frame's own shifts can hold is refused rather than shifted. */
    encoder_bissc_config_t bad;
    memset(&bad, 0, sizeof(bad));
    encoder_bissc_begin(&bad, 31u);
    assert_float_equal(encoder_bissc_frame(&bad, frame, 0.0f), 0.0f, 1e-6f);
}

typedef struct mock_ad2s1205_plant {
    uint16_t word;
    bool fail;
} mock_ad2s1205_plant_t;

static edge_status_t ad2s1205_plant_read(void *self, uint16_t *word) {
    mock_ad2s1205_plant_t *p = (mock_ad2s1205_plant_t *)self;
    if (p->fail) {
        return EDGE_ESTATE;
    }
    *word = p->word;
    return EDGE_OK;
}

/* The frame's own parity is odd, which is what makes the driver's fold read as an error when it is
 * not: an even count is the fault, so the parity bit is what a whole frame has to be given. */
static uint16_t ad2s1205_frame(uint16_t counts, uint16_t flags) {
    uint16_t word = (uint16_t)(((counts & 0x0FFFu) << 4) | (flags & 0x0Fu));
    uint16_t x = word;
    x ^= (uint16_t)(x >> 8);
    x ^= (uint16_t)(x >> 4);
    x ^= (uint16_t)(x >> 2);
    x ^= (uint16_t)(x >> 1);
    /* The low bit is set when the count is even, which is the fault; the parity bit fixes it. */
    if (((~x) & 1u) != 0u) {
        word ^= 0x0001u;
    }
    return word;
}

/*
 * enc_ad2s1205.c:66-176's routine: a frame of nothing at all is a converter that is not answering,
 * a frame without the read-velocity bit is a velocity one, the second and first bits are the chip's
 * own loss of tracking and loss of signal with a clear bit meaning the fault - and a loss of signal
 * is what both together mean, in which case the tracking flag is dropped - and the angle is the
 * twelve bits over a full turn, taken only when the parity held and none of the three flagged.
 */
static void test_encoder_ad2s1205_matches_the_reference(void **state) {
    (void)state;

    encoder_ad2s1205_state_t st;
    encoder_ad2s1205_begin(&st);
    mock_ad2s1205_plant_t plant = {0};
    encoder_ad2s1205_port_t port = {.self = &plant, .read_frame = ad2s1205_plant_read};

    /* A position packet of two thousand and forty-eight counts - half a turn - with nothing wrong.
     */
    plant.word = ad2s1205_frame(2048u, 0x0008u | 0x0006u); /* RDVEL set, both fault bits clear */
    assert_float_equal(encoder_ad2s1205_routine(&st, &port, 0.001f), 180.0f, 1e-3f);
    assert_int_equal(st.spi_error_cnt, 0u);
    assert_int_equal(st.resolver_loss_of_tracking_error_cnt, 0u);
    assert_int_equal(st.resolver_void_packet_cnt, 0u);

    /* A frame of nothing is the converter not answering, and the angle holds where it was. */
    const float held = st.last_enc_angle;
    plant.word = 0x0000u;
    encoder_ad2s1205_routine(&st, &port, 0.002f);
    assert_int_equal(st.resolver_void_packet_cnt, 1u);
    assert_float_equal(st.resolver_void_packet_error_rate, 0.001f, 1e-6f);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* A frame without the read-velocity bit is a velocity one, and is counted as its own error. */
    plant.word = ad2s1205_frame(2048u, 0x0006u); /* RDVEL clear */
    encoder_ad2s1205_routine(&st, &port, 0.003f);
    assert_int_equal(st.resolver_vel_packet_cnt, 1u);
    assert_float_equal(st.resolver_vel_packet_error_rate, 0.001f, 1e-6f);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* A clear tracking bit is the chip's own loss of tracking, and the angle is not taken. */
    plant.word = ad2s1205_frame(2048u, 0x0008u | 0x0004u); /* RDVEL set, tracking bit clear */
    encoder_ad2s1205_routine(&st, &port, 0.004f);
    assert_int_equal(st.resolver_loss_of_tracking_error_cnt, 1u);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* Both flags clear at once is a loss of signal, and the tracking flag is not counted with it.
     */
    const uint32_t lot_before = st.resolver_loss_of_tracking_error_cnt;
    plant.word = ad2s1205_frame(2048u, 0x0008u); /* both fault bits clear */
    encoder_ad2s1205_routine(&st, &port, 0.005f);
    assert_int_equal(st.resolver_loss_of_signal_error_cnt, 1u);
    assert_int_equal(st.resolver_loss_of_tracking_error_cnt, lot_before);
    assert_int_equal(st.resolver_degradation_of_signal_error_cnt, 0u);

    /* A frame whose parity does not hold is the bus's own error, and the angle is not taken. */
    plant.word = (uint16_t)(ad2s1205_frame(2048u, 0x0008u | 0x0006u) ^ 0x0001u);
    encoder_ad2s1205_routine(&st, &port, 0.006f);
    assert_int_equal(st.spi_error_cnt, 1u);
    assert_float_equal(st.spi_error_rate, 0.001f, 1e-6f);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    /* And the reset clears the counts, the rates and the peaks, but not the angle. */
    encoder_ad2s1205_reset_errors(&st);
    assert_int_equal(st.spi_error_cnt, 0u);
    assert_int_equal(st.resolver_void_packet_cnt, 0u);
    assert_int_equal(st.resolver_vel_packet_cnt, 0u);
    assert_int_equal(st.resolver_loss_of_tracking_error_cnt, 0u);
    assert_int_equal(st.resolver_loss_of_signal_error_cnt, 0u);
    assert_float_equal(st.spi_error_rate, 0.0f, 1e-6f);
    assert_float_equal(st.resolver_VOIDspi_peak_error_rate, 0.0f, 1e-6f);
    assert_float_equal(st.last_enc_angle, held, 1e-6f);

    encoder_ad2s1205_begin(NULL);
    encoder_ad2s1205_reset_errors(NULL);
    assert_float_equal(encoder_ad2s1205_routine(NULL, &port, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_ad2s1205_routine(&st, NULL, 0.0f), 0.0f, 1e-6f);
    encoder_ad2s1205_port_t bare = {0};
    assert_float_equal(encoder_ad2s1205_routine(&st, &bare, 0.0f), 0.0f, 1e-6f);
    /* A port that cannot read is a frame that cannot be judged, and the angle holds. */
    plant.fail = true;
    assert_float_equal(encoder_ad2s1205_routine(&st, &port, 0.007f), held, 1e-6f);
}

typedef struct mock_ma782_plant {
    bool ready;
    int starts;
    int flushes;
    uint8_t rx[4];
} mock_ma782_plant_t;

static void ma782_plant_flush(void *self) {
    ((mock_ma782_plant_t *)self)->flushes++;
}

static bool ma782_plant_ready(void *self) {
    return ((mock_ma782_plant_t *)self)->ready;
}

static void ma782_plant_start(void *self, const uint8_t tx[4], uint8_t rx[4]) {
    mock_ma782_plant_t *p = (mock_ma782_plant_t *)self;
    (void)tx;
    p->starts++;
    for (int i = 0; i < 4; i++) {
        rx[i] = p->rx[i];
    }
}

/*
 * enc_ma782.c:123-141's beginning of a read, :173-187's routine and :146-154's tail. The read is
 * only begun while the state is idle, the frame the sensor is given is noughts and the receive flag
 * is cleared first; the routine asks for one only when the bus is ready and the run has started;
 * and the tail makes the angle out of the frame's masked bits over a whole sixteen-bit turn.
 */
static void test_encoder_ma782_matches_the_reference(void **state) {
    (void)state;

    encoder_ma782_state_t st;
    encoder_ma782_begin(&st);
    mock_ma782_plant_t plant = {.ready = true};
    encoder_ma782_port_t port = {.self = &plant,
                                 .flush_rx = ma782_plant_flush,
                                 .spi_ready = ma782_plant_ready,
                                 .start_exchange = ma782_plant_start};

    /* The mask is the seven bits from the fourth up, which is what nine out of twelve leaves. */
    assert_int_equal(encoder_ma782_resolution_mask(), 0xFF80u);

    /* The run has not started, so the routine has nothing to ask for. */
    assert_true(encoder_ma782_routine(&st, &port));
    assert_int_equal(plant.starts, 0);

    st.start = 1u;
    assert_true(encoder_ma782_routine(&st, &port));
    assert_int_equal(plant.starts, 1);
    assert_int_equal(plant.flushes, 1);
    assert_int_equal(st.substate, ENCODER_MA782_READ_ANGLE_REQ);
    assert_int_equal(st.tx_buf[0], 0u);

    /* A read asked for while one is outstanding is the reference's own error. */
    assert_false(encoder_ma782_read_angle(&st, &port));
    assert_int_equal(st.error & ENCODER_MA782_ANGLE_NOT_IDLE, ENCODER_MA782_ANGLE_NOT_IDLE);

    /* The tail: the frame's masked bits over a sixteen-bit turn, which is the reference's own
     * denominator rather than the mask's. */
    st.rx_data = 0x1234u;
    assert_float_equal(encoder_ma782_read_angle_finish(&st),
                       (float)(0x1234u & 0xFF80u) * (360.0f / 65535.0f), 1e-3f);
    assert_int_equal(st.substate, ENCODER_MA782_IDLE);

    /* A bus that is not ready raises both the count and its own flag. */
    plant.ready = false;
    assert_false(encoder_ma782_routine(&st, &port));
    assert_int_equal(st.spi_comm_error_cnt, 1u);
    assert_int_equal(st.error & ENCODER_MA782_SPI_NOT_READY, ENCODER_MA782_SPI_NOT_READY);
    assert_float_equal(st.spi_comm_error_rate, 0.0001f, 1e-7f);

    encoder_ma782_begin(NULL);
    encoder_ma782_error(NULL, 1u);
    assert_float_equal(encoder_ma782_read_angle_finish(NULL), 0.0f, 1e-6f);
    assert_false(encoder_ma782_read_angle(&st, NULL));
    assert_false(encoder_ma782_read_angle(NULL, &port));
    assert_false(encoder_ma782_routine(&st, NULL));
    assert_false(encoder_ma782_routine(NULL, &port));
    encoder_ma782_port_t bare_ma = {0};
    assert_false(encoder_ma782_routine(&st, &bare_ma));
}

/*
 * enc_ts5700n8501.c:168-206's own decode, on the frame the sensor sends: eleven bytes whose last is
 * the exclusive or of the ten before it, a position little-endian in the three after the first over
 * its own counts of a turn, and the eight status bytes kept as they came.
 */
static void test_encoder_ts5700_matches_the_reference(void **state) {
    (void)state;

    encoder_ts5700_state_t st;
    encoder_ts5700_begin(&st);

    uint8_t reply[ENCODER_TS5700_REPLY_LEN] = {0};
    reply[1] = 0xA5u;                     /* SF */
    reply[2] = (uint8_t)(32768u & 0xFFu); /* the position's own three bytes, little-endian */
    reply[3] = (uint8_t)((32768u >> 8) & 0xFFu);
    reply[4] = (uint8_t)((32768u >> 16) & 0xFFu);
    reply[6] = 0x01u;
    reply[9] = 0x02u;
    uint8_t crc = 0u;
    for (int i = 0; i < (int)ENCODER_TS5700_REPLY_LEN - 1; i++) {
        crc = (uint8_t)(reply[i] ^ crc);
    }
    reply[ENCODER_TS5700_REPLY_LEN - 1] = crc;

    /* A quarter of the sensor's own counts is a quarter turn. */
    assert_float_equal(encoder_ts5700_decode(&st, reply, 0.001f), 90.0f, 1e-3f);
    assert_int_equal(st.spi_val, 32768u);
    assert_int_equal(st.spi_error_cnt, 0u);
    assert_int_equal(st.raw_status[0], 0xA5u);
    assert_int_equal(st.raw_status[1], reply[2]);
    assert_int_equal(st.raw_status[4], reply[6]);
    assert_int_equal(st.raw_status[7], reply[9]);

    /* A frame whose exclusive or does not hold raises the count and leaves the angle where it was.
     */
    const float held = st.last_enc_angle;
    reply[ENCODER_TS5700_REPLY_LEN - 1] ^= 0x01u;
    assert_float_equal(encoder_ts5700_decode(&st, reply, 0.002f), held, 1e-6f);
    assert_int_equal(st.spi_error_cnt, 1u);
    assert_float_equal(st.spi_error_rate, 0.002f, 1e-6f);

    encoder_ts5700_begin(NULL);
    assert_float_equal(encoder_ts5700_decode(NULL, reply, 0.0f), 0.0f, 1e-6f);
    assert_float_equal(encoder_ts5700_decode(&st, NULL, 0.0f), 0.0f, 1e-6f);
}

/*
 * enc_as5x47u.c:90-115's table and :356's transmitting CRC. Every entry is held against the
 * polynomial folded by hand, and the two bytes of each of the reference's own reads are run through
 * the table to see whether the values it checks in are what that fold gives.
 */
static void test_encoder_as5x47u_matches_the_reference(void **state) {
    (void)state;

    encoder_as5x47u_state_t st;
    encoder_as5x47u_begin(&st);

    for (int i = 0; i < 256; i++) {
        uint8_t crc = (uint8_t)i;
        for (int bit = 0; bit < 8; bit++) {
            const uint32_t shifted = (uint32_t)(crc << 1);
            crc = (uint8_t)(((crc & 0x80u) != 0u) ? (shifted ^ 0x1Du) : shifted);
        }
        assert_int_equal(st.table_crc8[i], crc);
    }

    const uint16_t messages[5] = {(uint16_t)(AS5X47U_SPI_ERRFL_ADR | AS5X47U_SPI_READ_BIT),
                                  (uint16_t)(AS5X47U_SPI_DIAG_ADR | AS5X47U_SPI_READ_BIT),
                                  (uint16_t)(AS5X47U_SPI_MAGN_ADR | AS5X47U_SPI_READ_BIT),
                                  (uint16_t)(AS5X47U_SPI_AGC_ADR | AS5X47U_SPI_READ_BIT),
                                  (uint16_t)(AS5X47U_SPI_POS_ADR | AS5X47U_SPI_READ_BIT)};
    const uint8_t expected[5] = {AS5X47U_SPI_READ_ERRFL_CRC, AS5X47U_SPI_READ_DIAG_CRC,
                                 AS5X47U_SPI_READ_MAGN_CRC, AS5X47U_SPI_READ_AGC_CRC,
                                 AS5X47U_SPI_READ_POS_CRC};
    for (int i = 0; i < 5; i++) {
        const uint8_t bytes[2] = {(uint8_t)(messages[i] >> 8), (uint8_t)(messages[i] & 0xFFu)};
        assert_int_equal(encoder_as5x47u_crc8(st.table_crc8, bytes, 2u, AS5X47U_SPI_TX_CRC_SEED) ^
                             0xFFu,
                         expected[i]);
    }

    /* The transmitting CRC takes the two bytes of a frame and the reference's own seed. */
    assert_int_equal(
        encoder_as5x47u_transmit_crc(st.table_crc8, messages[4]),
        (uint8_t)(encoder_as5x47u_crc8(st.table_crc8,
                                       (const uint8_t[]){(uint8_t)(messages[4] >> 8),
                                                         (uint8_t)(messages[4] & 0xFFu)},
                                       2u, AS5X47U_SPI_TX_CRC_SEED) ^
                  0xFFu));

    /* The position's own reading: the low fourteen bits over a quarter turn, error bit and all. */
    encoder_as5x47u_process_pos(&st, (uint16_t)(4096u | 0x4000u));
    assert_int_equal(st.spi_val, (uint16_t)(4096u | 0x4000u));
    assert_float_equal(st.last_enc_angle, (float)(4096u * 360u) / (float)(1u << 14), 1e-3f);

    encoder_as5x47u_begin(NULL);
    encoder_as5x47u_build_crc_table(NULL);
    assert_int_equal(encoder_as5x47u_crc8(NULL, expected, 1u, 0u), 0u);
    assert_int_equal(encoder_as5x47u_crc8(st.table_crc8, NULL, 1u, 0u), 0u);
    encoder_as5x47u_process_pos(NULL, 1u);
}

/* A frame of two data bytes and the CRC the family's own fold puts on it. */
static void as5x47u_frame(uint8_t rx[3], const uint8_t table[256], uint16_t data) {
    rx[0] = (uint8_t)(data >> 8);
    rx[1] = (uint8_t)(data & 0xFFu);
    rx[2] = (uint8_t)(encoder_as5x47u_crc8(table, rx, 2u, AS5X47U_SPI_TX_CRC_SEED) ^ 0xFFu);
}

/*
 * enc_as5x47u.c:119-245's callback: a frame whose CRC holds moves the sequence one step and answers
 * with the message to ask for next. Six of the eight states are a position frame answered by asking
 * for another while the next reading is collected, and the other two take the magnitude, the AGC,
 * the four diagnostics and the three error flags. A frame whose CRC does not hold, or that carries
 * the sensor's own error bit, takes the sequence to its error state, which asks for the flags
 * instead.
 */
static void test_encoder_as5x47u_sequence_matches_the_reference(void **state) {
    (void)state;

    encoder_as5x47u_state_t st;
    encoder_as5x47u_begin(&st);
    assert_int_equal(st.spi_seq, ENCODER_AS5X47U_SEQ_TX_MAG_RX_POS);

    uint8_t rx[3];
    const uint16_t pos_message = (uint16_t)(AS5X47U_SPI_POS_ADR | AS5X47U_SPI_READ_BIT);

    /* A position frame at the start: the angle is taken and the sequence asks for another position.
     */
    as5x47u_frame(rx, st.table_crc8, 4096u);
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.001f), pos_message);
    assert_float_equal(st.last_enc_angle, (float)(4096u * 360u) / (float)(1u << 14), 1e-3f);
    assert_int_equal(st.spi_seq, ENCODER_AS5X47U_SEQ_TX_POS_RX_MAG);
    assert_true(st.sensor_diag.is_connected);

    /* The magnitude it asked for. */
    as5x47u_frame(rx, st.table_crc8, 0x1234u);
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.002f),
                     (uint16_t)(AS5X47U_SPI_AGC_ADR | AS5X47U_SPI_READ_BIT));
    assert_int_equal(st.sensor_diag.serial_magnitude, 0x1234u);
    assert_int_equal(st.sensor_diag.magnitude, 0x1234u & 0x3FFFu);

    /* A position frame again, then the AGC. */
    as5x47u_frame(rx, st.table_crc8, 2048u);
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.003f), pos_message);
    as5x47u_frame(rx, st.table_crc8, 0x00ABu);
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.004f),
                     (uint16_t)(AS5X47U_SPI_DIAG_ADR | AS5X47U_SPI_READ_BIT));
    assert_int_equal(st.sensor_diag.AGC_value, 0xABu);

    /* A position frame, then the diagnostics: the four bits the reference takes out of them. */
    as5x47u_frame(rx, st.table_crc8, 1024u);
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.005f), pos_message);
    as5x47u_frame(rx, st.table_crc8, (uint16_t)((1u << 10) | (1u << 2) | (1u << 3)));
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.006f),
                     (uint16_t)(AS5X47U_SPI_ERRFL_ADR | AS5X47U_SPI_READ_BIT));
    assert_int_equal(st.sensor_diag.is_broken_hall, 1u);
    assert_int_equal(st.sensor_diag.is_COF, 1u);
    assert_int_equal(st.sensor_diag.is_Comp_low, 1u);
    assert_int_equal(st.sensor_diag.is_Comp_high, 0u);

    /* A position frame, then the error flags: those three bits, and the cycle begins again. */
    as5x47u_frame(rx, st.table_crc8, 512u);
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.007f), pos_message);
    as5x47u_frame(rx, st.table_crc8, (uint16_t)((1u << 7) | (1u << 6) | (1u << 1)));
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.008f),
                     (uint16_t)(AS5X47U_SPI_MAGN_ADR | AS5X47U_SPI_READ_BIT));
    assert_int_equal(st.sensor_diag.is_wdt, 1u);
    assert_int_equal(st.sensor_diag.is_crc_error, 1u);
    assert_int_equal(st.sensor_diag.is_mag_half, 1u);
    assert_int_equal(st.spi_seq, ENCODER_AS5X47U_SEQ_TX_MAG_RX_POS);

    /* A frame whose CRC does not hold is the bus's own error: the flags are asked for instead. */
    as5x47u_frame(rx, st.table_crc8, 4096u);
    rx[2] ^= 0x01u;
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.009f),
                     (uint16_t)(AS5X47U_SPI_ERRFL_ADR | AS5X47U_SPI_READ_BIT));
    assert_int_equal(st.spi_seq, ENCODER_AS5X47U_SEQ_PREV_ERR);
    assert_int_equal(st.sensor_diag.spi_error_cnt, 1u);
    assert_float_equal(st.sensor_diag.spi_error_rate, 0.001f, 1e-6f);

    /* And the sensor's own error bit, on a frame whose CRC does hold, is the same way out. */
    as5x47u_frame(rx, st.table_crc8, (uint16_t)0x8000u);
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.010f),
                     (uint16_t)(AS5X47U_SPI_ERRFL_ADR | AS5X47U_SPI_READ_BIT));
    assert_int_equal(st.sensor_diag.is_error, 1u);

    /* The error state keeps asking for the flags while its own frames answer nothing useful. */
    assert_int_equal(encoder_as5x47u_callback(&st, rx, 0.011f),
                     (uint16_t)(AS5X47U_SPI_ERRFL_ADR | AS5X47U_SPI_READ_BIT));

    assert_int_equal(encoder_as5x47u_callback(NULL, rx, 0.0f), 0u);
    assert_int_equal(encoder_as5x47u_callback(&st, NULL, 0.0f), 0u);
}

/*
 * enc_tle5012.c:304-324's CRC-8 and :328-380's safety word. The checksum is held against the
 * polynomial folded by hand with the family's own seed, the three bits are the reference's own
 * inversions - a clear bit is the fault, not one that is set - and the checksum runs over the
 * command word and every data word of the answer.
 */
static void test_encoder_tle5012_matches_the_reference(void **state) {
    (void)state;

    const uint8_t bytes[4] = {0x83u, 0x00u, 0x12u, 0x34u};
    uint32_t crc = 0xFFu;
    for (int i = 0; i < 4; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            const uint32_t shifted = (crc << 1);
            crc = ((crc & 0x80u) != 0u) ? (shifted ^ 0x1Du) : shifted;
        }
    }
    assert_int_equal(encoder_tle5012_crc8(bytes, 4u), (uint8_t)((~crc) & 0xFFu));

    /* A whole answer: the three bits set and the checksum the family's own fold gives. */
    const uint16_t command = 0x8300u;
    const uint16_t words[1] = {0x1234u};
    uint8_t frame[4] = {(uint8_t)(command >> 8), (uint8_t)command, (uint8_t)(words[0] >> 8),
                        (uint8_t)words[0]};
    const uint8_t sum = encoder_tle5012_crc8(frame, 4u);
    assert_int_equal(encoder_tle5012_check_safety(command, (uint16_t)(0x7000u | sum), words, 1u),
                     ENCODER_TLE5012_NO_ERROR);

    /* Each of the three bits, cleared in turn, is its own verdict. */
    assert_int_equal(encoder_tle5012_check_safety(command, (uint16_t)(0x3000u | sum), words, 1u),
                     ENCODER_TLE5012_SYSTEM_ERROR);
    assert_int_equal(encoder_tle5012_check_safety(command, (uint16_t)(0x5000u | sum), words, 1u),
                     ENCODER_TLE5012_INTERFACE_ACCESS_ERROR);
    assert_int_equal(encoder_tle5012_check_safety(command, (uint16_t)(0x6000u | sum), words, 1u),
                     ENCODER_TLE5012_INVALID_ANGLE_ERROR);

    /* And a checksum that does not hold is a verdict of its own. */
    assert_int_equal(
        encoder_tle5012_check_safety(command, (uint16_t)(0x7000u | (sum ^ 0x01u)), words, 1u),
        ENCODER_TLE5012_CRC_ERROR);

    /* The angle over two to the fifteenth of a turn. */
    assert_float_equal(encoder_tle5012_pos_to_deg(8192u), 90.0f, 1e-3f);

    assert_int_equal(encoder_tle5012_crc8(NULL, 4u), 0u);
    assert_int_equal(encoder_tle5012_check_safety(command, 0x7000u, NULL, 1u),
                     ENCODER_TLE5012_CRC_ERROR);
}

typedef struct mock_core_plant {
    float deg;
    int abi_sets;
    int custom_sets;
    float last_abi_deg;
    float last_custom_deg;
    bool abi_index;
    bool pwm_ready;
    float ts_mt;
    int mt_resets;
    int err_resets;
} mock_core_plant_t;

static float core_plant_read(void *self) {
    return ((mock_core_plant_t *)self)->deg;
}

static void core_plant_set_abi(void *self, float deg) {
    mock_core_plant_t *p = (mock_core_plant_t *)self;
    p->abi_sets++;
    p->last_abi_deg = deg;
}

static void core_plant_set_custom(void *self, float deg) {
    mock_core_plant_t *p = (mock_core_plant_t *)self;
    p->custom_sets++;
    p->last_custom_deg = deg;
}

static bool core_plant_abi_index(void *self) {
    return ((mock_core_plant_t *)self)->abi_index;
}

static bool core_plant_pwm_ready(void *self) {
    return ((mock_core_plant_t *)self)->pwm_ready;
}

static float core_plant_ts_mt(void *self) {
    return ((mock_core_plant_t *)self)->ts_mt;
}

static void core_plant_mt_reset(void *self) {
    ((mock_core_plant_t *)self)->mt_resets++;
}

static void core_plant_err_reset(void *self) {
    ((mock_core_plant_t *)self)->err_resets++;
}

/*
 * encoder.c:132-181's own bookkeeping: the reading is whichever family's the caller wired, the
 * multiturn count belongs to the one sensor that has it and is carried in ten-thousandths of a turn
 * beside a ten-thousandth of the angle - dropped once it steps past five thousand - and an angle a
 * caller sets is normalised before only the two types that can hold one are told. The index is
 * found for every type but the two that have to earn it.
 */
static void test_encoder_core_matches_the_reference(void **state) {
    (void)state;

    mock_core_plant_t plant = {.deg = 123.0f, .abi_index = true, .pwm_ready = true, .ts_mt = 12.0f};
    encoder_core_port_t port = {.self = &plant,
                                .set_abi_deg = core_plant_set_abi,
                                .set_custom_deg = core_plant_set_custom,
                                .abi_index_found = core_plant_abi_index,
                                .pwm_abi_ready = core_plant_pwm_ready,
                                .ts_multiturn = core_plant_ts_mt,
                                .ts_reset_multiturn = core_plant_mt_reset,
                                .ts_reset_errors = core_plant_err_reset};
    encoder_core_t core;

    /* A sensor with nothing special about it: read as its family read, index always found. */
    encoder_core_begin(&core, ENCODER_CORE_TYPE_SINCOS, core_plant_read, &plant);
    assert_int_equal(encoder_core_is_configured(&core), ENCODER_CORE_TYPE_SINCOS);
    assert_float_equal(encoder_core_read_deg(&core), 123.0f, 1e-6f);
    assert_float_equal(encoder_core_read_deg_multiturn(&core, &port), 123.0f, 1e-6f);
    assert_true(encoder_core_index_found(&core, &port));
    encoder_core_set_deg(&core, &port, 190.0f);
    assert_int_equal(plant.abi_sets, 0);
    assert_int_equal(plant.custom_sets, 0);
    assert_float_equal(core.custom_deg, 0.0f, 1e-6f);

    /* The multiturn sensor: the count in ten-thousandths beside the angle's own ten-thousandth. */
    encoder_core_begin(&core, ENCODER_CORE_TYPE_TS5700N8501, core_plant_read, &plant);
    assert_float_equal(encoder_core_read_deg_multiturn(&core, &port),
                       123.0f / 10000.0f + (360.0f * (12.0f + 5000.0f)) / 10000.0f, 1e-3f);

    /* A count past five thousand is dropped, and the sensor is told to reset it. */
    plant.ts_mt = 6000.0f;
    assert_float_equal(encoder_core_read_deg_multiturn(&core, &port),
                       123.0f / 10000.0f + (360.0f * 5000.0f) / 10000.0f, 1e-3f);
    assert_int_equal(plant.mt_resets, 1);
    encoder_core_reset_multiturn(&core, &port);
    assert_int_equal(plant.mt_resets, 2);
    encoder_core_reset_errors(&core, &port);
    assert_int_equal(plant.err_resets, 1);

    /* The incremental one: a set angle is normalised, goes to its counter, and is its own index. */
    encoder_core_begin(&core, ENCODER_CORE_TYPE_ABI, core_plant_read, &plant);
    assert_true(encoder_core_index_found(&core, &port));
    encoder_core_set_deg(&core, &port, 190.0f);
    assert_int_equal(plant.abi_sets, 1);
    assert_float_equal(plant.last_abi_deg, -170.0f, 1e-4f);
    assert_float_equal(encoder_core_abi_deg_to_count(90.0f, 4096.0f), 1024.0f, 1e-3f);
    plant.abi_index = false;
    assert_false(encoder_core_index_found(&core, &port));

    /* A sensor that is none of the others keeps the angle it was given. */
    encoder_core_begin(&core, ENCODER_CORE_TYPE_CUSTOM, core_plant_read, &plant);
    encoder_core_set_deg(&core, &port, -190.0f);
    assert_float_equal(core.custom_deg, 170.0f, 1e-4f);
    assert_float_equal(plant.last_custom_deg, 170.0f, 1e-4f);

    /* The PWM-ABI's index is the one that has to be earned, and it is the port's to say. */
    encoder_core_begin(&core, ENCODER_CORE_TYPE_PWM_ABI, core_plant_read, &plant);
    assert_true(encoder_core_index_found(&core, &port));
    plant.pwm_ready = false;
    assert_false(encoder_core_index_found(&core, &port));

    /* No core, no port. */
    encoder_core_begin(NULL, ENCODER_CORE_TYPE_NONE, NULL, NULL);
    assert_float_equal(encoder_core_read_deg(NULL), 0.0f, 1e-6f);
    assert_int_equal(encoder_core_is_configured(NULL), ENCODER_CORE_TYPE_NONE);
    assert_false(encoder_core_index_found(NULL, &port));
    encoder_core_set_deg(NULL, &port, 1.0f);
    encoder_core_reset_multiturn(NULL, &port);
    encoder_core_reset_errors(NULL, &port);
    assert_float_equal(encoder_core_read_deg_multiturn(NULL, &port), 0.0f, 1e-6f);
    encoder_core_begin(&core, ENCODER_CORE_TYPE_TS5700N8501, NULL, NULL);
    assert_float_equal(encoder_core_read_deg(&core), 0.0f, 1e-6f);
    encoder_core_set_deg(&core, NULL, 1.0f);
    encoder_core_reset_multiturn(&core, NULL);
    encoder_core_reset_errors(&core, NULL);
    assert_float_equal(encoder_core_read_deg_multiturn(&core, NULL), 0.0f, 1e-6f);
}

/*
 * encoder.c:186-…'s own fault policy: in force only while the encoder is the thing the motor
 * commutes from, and then per mode - the AS504x's bus rate and its two magnet flags beside its
 * connection one, the MT6816's own missing-magnet rate, and the MT6835's bus rate - each past a
 * twentieth, which is the reference's own threshold and its own strict comparison.
 */
static void test_encoder_core_faults_match_the_reference(void **state) {
    (void)state;

    encoder_fault_inputs_t in = {0};

    /* Nothing in use is no fault, whatever the rates say. */
    in.as504x_spi_error_rate = 1.0f;
    assert_int_equal(encoder_core_check_faults(false, ENCODER_PORT_MODE_AS5047_SPI, &in),
                     ENCODER_FAULT_NONE);

    /* The AS504x: a bus rate past a twentieth is the bus's own fault. */
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_AS5047_SPI, &in),
                     ENCODER_FAULT_SPI);

    /* At the threshold itself it is not: the reference's comparison is a strict one. */
    in.as504x_spi_error_rate = ENCODER_FAULT_ERROR_RATE_THRESHOLD;
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_AS5047_SPI, &in),
                     ENCODER_FAULT_NONE);

    /* With a MOSI line, a connection that has been given up on is the bus's fault too. */
    in.as504x_has_mosi = true;
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_AS5047_SPI, &in),
                     ENCODER_FAULT_SPI);

    /* And the two magnet flags are their own verdicts, the high one first. */
    in.as504x_is_connected = true;
    in.as504x_is_comp_high = true;
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_AS5047_SPI, &in),
                     ENCODER_FAULT_NO_MAGNET);
    in.as504x_is_comp_high = false;
    in.as504x_is_comp_low = true;
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_AS5047_SPI, &in),
                     ENCODER_FAULT_MAGNET_TOO_STRONG);

    /* The MT6816 reports a missing magnet as a rate of its own. */
    encoder_fault_inputs_t mt = {.mt6816_no_magnet_error_rate = 0.06f};
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_MT6816_SPI_HW, &mt),
                     ENCODER_FAULT_NO_MAGNET);
    mt.mt6816_no_magnet_error_rate = 0.04f;
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_MT6816_SPI_HW, &mt),
                     ENCODER_FAULT_NONE);

    /* The MT6835's bus rate is its bus's fault. */
    encoder_fault_inputs_t mt5 = {.mt6835_spi_error_rate = 0.06f};
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_MT6835_SPI_HW, &mt5),
                     ENCODER_FAULT_SPI);

    /* A mode this policy has not been read for answers none, and so does having no inputs. */
    assert_int_equal(encoder_core_check_faults(true, 99u, &in), ENCODER_FAULT_NONE);
    assert_int_equal(encoder_core_check_faults(true, ENCODER_PORT_MODE_AS5047_SPI, NULL),
                     ENCODER_FAULT_NONE);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_encoder_core_faults_match_the_reference),
        cmocka_unit_test(test_encoder_core_matches_the_reference),
        cmocka_unit_test(test_encoder_tle5012_matches_the_reference),
        cmocka_unit_test(test_encoder_as5x47u_sequence_matches_the_reference),
        cmocka_unit_test(test_encoder_as5x47u_matches_the_reference),
        cmocka_unit_test(test_encoder_ts5700_matches_the_reference),
        cmocka_unit_test(test_encoder_ma782_matches_the_reference),
        cmocka_unit_test(test_encoder_ad2s1205_matches_the_reference),
        cmocka_unit_test(test_encoder_as504x_matches_the_reference),
        cmocka_unit_test(test_encoder_mt6816_matches_the_reference),
        cmocka_unit_test(test_encoder_abi_index_machine),
        cmocka_unit_test(test_encoder_bissc_matches_the_reference),
        cmocka_unit_test(test_encoder_mt6835_matches_the_reference),
        cmocka_unit_test(test_encoder_pwm_matches_the_reference),
        cmocka_unit_test(test_encoder_amt22_matches_the_reference),
        cmocka_unit_test(test_encoder_sincos_matches_the_reference),
        cmocka_unit_test(test_encoder_resolver_and_hall_guards),
        cmocka_unit_test(test_encoder_spi_failure_and_guards),
        cmocka_unit_test(test_encoder_response_checks_and_wraps),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
