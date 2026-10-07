/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>
/* clang-format on */

#include "pas/pas.h"

/*
 * applications/app_pas.c reads two pad levels, a torque sensor as a ratio and a free-running clock.
 * The mock carries all three; the cases walk the levels the way the quadrature decoder does.
 */
typedef struct mock_pas {
    uint8_t level1;
    uint8_t level2;
    float torque_ratio;
    float now_s;
    bool levels_ok;
} mock_pas_t;

static edge_status_t mock_read_levels(void *self, uint8_t *pas1, uint8_t *pas2) {
    mock_pas_t *m = (mock_pas_t *)self;
    if (!m->levels_ok) {
        return EDGE_EIO;
    }
    *pas1 = m->level1;
    *pas2 = m->level2;
    return EDGE_OK;
}

static edge_status_t mock_read_torque_ratio(void *self, float *ratio) {
    mock_pas_t *m = (mock_pas_t *)self;
    *ratio = m->torque_ratio;
    return EDGE_OK;
}

static edge_status_t mock_now_seconds(void *self, float *seconds) {
    mock_pas_t *m = (mock_pas_t *)self;
    *seconds = m->now_s;
    return EDGE_OK;
}

static pas_config_t default_config(void) {
    pas_config_t cfg = {
        .ctrl_type = PAS_MODE_CADENCE,
        .sensor_type = PAS_SENSOR_QUADRATURE,
        .current_scaling = 1.0f,
        .pedal_rpm_start = 30.0f,
        .pedal_rpm_end = 90.0f,
        .invert_pedal_direction = false,
        .magnets = 1u,
        .use_filter = false,
        .ramp_time_pos = 0.0f,
        .ramp_time_neg = 0.0f,
        .update_rate_hz = 50u,
    };
    return cfg;
}

static void make_app(pas_app_t *app, mock_pas_t *m, const pas_config_t *cfg) {
    pas_port_t port = {
        .self = m,
        .read_levels = mock_read_levels,
        .read_torque_ratio = mock_read_torque_ratio,
        .now_seconds = mock_now_seconds,
    };
    pas_construct(app, EDGE_MOD_PAS, 20u, cfg, &port);
    assert_int_equal(pas_init(app), EDGE_OK);
}

static void step_state(mock_pas_t *m, uint8_t state) {
    m->level1 = (uint8_t)(state & 1u);
    m->level2 = (uint8_t)((state >> 1u) & 1u);
}

/*
 * The forward walk of the quadrature cycle - 0, 2, 3, 1 - with the clock moved on far enough that
 * the period is above the shortest the application will believe. Twelve steps is three cycles,
 * which is what the four-events-in-a-row rule needs before the first rising edge counts.
 */
static void prime_cadence(pas_app_t *app, mock_pas_t *m, float start_s) {
    static const uint8_t cycle[] = {0u, 2u, 3u, 1u};
    for (int i = 0; i < 12; i++) {
        m->now_s = start_s + 0.08f * (float)i;
        step_state(m, cycle[i % 4]);
        pas_event_handler(app);
    }
}

static void test_pas_init_guards(void **state) {
    (void)state;
    pas_app_t app;
    pas_config_t cfg = default_config();
    mock_pas_t m;
    memset(&m, 0, sizeof(m));
    m.levels_ok = true;

    pas_construct(&app, EDGE_MOD_PAS, 20u, &cfg, NULL);
    assert_int_equal(pas_init(&app), EDGE_EINVAL); /* no port at all */
    assert_int_equal(pas_init(NULL), EDGE_EINVAL);

    pas_port_t empty = {.self = &m};
    pas_construct(&app, EDGE_MOD_PAS, 20u, &cfg, &empty);
    assert_int_equal(pas_init(&app), EDGE_EINVAL); /* no reader and no clock */

    assert_int_equal(pas_update(NULL, 0.02f), EDGE_EINVAL);

    /* Nothing is commanded before anything is read, and every guard answers. */
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-9f);
    assert_float_equal(pas_get_current_target_rel(NULL), 0.0f, 1e-9f);
    assert_float_equal(pas_get_pedal_rpm(NULL), 0.0f, 1e-9f);
    assert_false(pas_is_primary_output(NULL));
    assert_false(pas_is_active(NULL));
    assert_null(pas_module(NULL));
    pas_set_primary_output(NULL, true);
    pas_set_fault(NULL, true);
    pas_set_current_sub_scaling(NULL, 2.0f);
}

/*
 * The decoder: four quadrature events in the right direction are required, and the speed comes from
 * one rising edge of the pair against the previous one. A reading that fails leaves the last speed
 * alone, and a reverse walk drives it to zero rather than negative.
 */
static void test_pas_event_handler_rpm(void **state) {
    (void)state;
    pas_app_t app;
    mock_pas_t m;
    memset(&m, 0, sizeof(m));
    m.levels_ok = true;
    pas_config_t cfg = default_config();
    make_app(&app, &m, &cfg);

    /* The first three cycles count the direction but the first accepted edge measures from zero, so
     * a walk shorter than the shortest believable period leaves the speed where it was. */
    for (int i = 0; i < 12; i++) {
        m.now_s = 0.001f * (float)i;
        step_state(&m, (uint8_t)((i % 4) == 0 ? 0u : (i % 4 == 1 ? 2u : (i % 4 == 2 ? 3u : 1u))));
        pas_event_handler(&app);
    }

    prime_cadence(&app, &m, 1.0f);
    const float rpm = pas_get_pedal_rpm(&app);
    assert_true(rpm > 0.0f);
    assert_true(rpm < 90.0f * 3.0f); /* the application's own ceiling on what it will believe */

    /* A reading that fails changes nothing. */
    m.levels_ok = false;
    pas_event_handler(&app);
    assert_float_equal(pas_get_pedal_rpm(&app), rpm, 1e-6f);

    /* The other way round: the count wants four events in the direction the configuration calls
     * forward, so a reverse walk never reaches it and the speed it reports is left where it was. */
    m.levels_ok = true;
    static const uint8_t reverse[] = {0u, 1u, 3u, 2u};
    for (int i = 0; i < 12; i++) {
        m.now_s = 2.0f + 0.08f * (float)i;
        step_state(&m, reverse[i % 4]);
        pas_event_handler(&app);
    }
    assert_float_equal(pas_get_pedal_rpm(&app), rpm, 1e-6f);

    /* With the direction inverted the reverse walk becomes the forward one: two signs meet, so the
     * speed it reports is positive - the sign is the decoder's own product, not this application's
     * preference. */
    pas_config_t inv = cfg;
    inv.invert_pedal_direction = true;
    make_app(&app, &m, &inv);
    for (int i = 0; i < 12; i++) {
        m.now_s = 5.0f + 0.08f * (float)i;
        step_state(&m, reverse[i % 4]);
        pas_event_handler(&app);
    }
    assert_true(pas_get_pedal_rpm(&app) > 0.0f);
}

/*
 * Cadence: the idle timer the reference keeps for it, the bounding by the scaling, and the on/off
 * case that replaces the map when both limits are the same value.
 */
static void test_pas_cadence_mode(void **state) {
    (void)state;
    pas_app_t app;
    mock_pas_t m;
    memset(&m, 0, sizeof(m));
    m.levels_ok = true;
    pas_config_t cfg = default_config();
    cfg.current_scaling = 2.0f;
    make_app(&app, &m, &cfg);

    /* Held at zero while the safe start's clock runs up: it counts the passes with no output, and
     * because the count is compared with itself it has to hold still twice - five hundred passes.
     */
    for (int i = 0; i < 502; i++) {
        assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    }
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-6f);

    /* Pedalling now, the output is bounded by the scaling and the period's own decay is what it is.
     */
    prime_cadence(&app, &m, 3.0f);
    assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    const float out = pas_get_current_target_rel(&app);
    assert_true(out >= 0.0f);
    assert_true(out <= 2.0f + 1e-6f);

    /* Both limits the same means on or off rather than a map that divides by zero. */
    pas_config_t eq = default_config();
    eq.pedal_rpm_start = 200.0f;
    eq.pedal_rpm_end = 200.0f;
    eq.current_scaling = 3.0f;
    make_app(&app, &m, &eq);
    for (int i = 0; i < 502; i++) {
        assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    }
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-6f);
}

/*
 * Torque: the reference holds this mode at zero while the safe start counts, and that count only
 * advances while the output is at zero - so a torque that produces an output stops the count that
 * would let it through. That is the reference's own arrangement, not this port's, and it is
 * asserted rather than smoothed over.
 */
static void test_pas_torque_mode_holds(void **state) {
    (void)state;
    pas_app_t app;
    mock_pas_t m;
    memset(&m, 0, sizeof(m));
    m.levels_ok = true;
    m.torque_ratio = 0.5f;
    pas_config_t cfg = default_config();
    cfg.ctrl_type = PAS_MODE_TORQUE;
    cfg.current_scaling = 2.0f;
    make_app(&app, &m, &cfg);

    for (int i = 0; i < 100; i++) {
        assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    }
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-6f);

    /* And with no torque at all the same thing happens, for the other of the two reasons. */
    m.torque_ratio = 0.0f;
    for (int i = 0; i < 100; i++) {
        assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    }
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-6f);
}

/* The safe start's clock, the fault that re-arms it, and the power-off hook. */
static void test_pas_safe_start_and_power_off(void **state) {
    (void)state;
    pas_app_t app;
    mock_pas_t m;
    memset(&m, 0, sizeof(m));
    m.levels_ok = true;
    pas_config_t cfg = default_config();
    cfg.ctrl_type = PAS_MODE_NONE;
    make_app(&app, &m, &cfg);

    assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    assert_true(pas_is_active(&app));
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-9f);

    /* Nothing commands anything in this mode, so the clock runs up rather than holding. */
    for (int i = 0; i < 30; i++) {
        assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    }
    pas_set_fault(&app, true);
    assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    pas_set_fault(&app, false);
    assert_int_equal(pas_update(&app, 0.02f), EDGE_OK);
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-9f);

    /* The primary-output flag and the sub-scaling are the product's to set. */
    pas_set_primary_output(&app, true);
    assert_true(pas_is_primary_output(&app));
    pas_set_current_sub_scaling(&app, 0.5f);

    edge_module_t *mod = pas_module(&app);
    assert_non_null(mod);
    assert_int_equal(mod->power_off(mod), EDGE_OK);
    assert_float_equal(pas_get_current_target_rel(&app), 0.0f, 1e-9f);
    assert_false(pas_is_active(&app));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pas_init_guards),
        cmocka_unit_test(test_pas_event_handler_rpm),
        cmocka_unit_test(test_pas_cadence_mode),
        cmocka_unit_test(test_pas_torque_mode_holds),
        cmocka_unit_test(test_pas_safe_start_and_power_off),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
