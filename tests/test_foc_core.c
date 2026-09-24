#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/errors.h"
#include "edge/modules.h"
#include "foc_core/foc_core.h"
#include "foc_core/foc_math.h"

/* Inverter mock */
typedef struct mock_inverter {
    float duty_a;
    float duty_b;
    float duty_c;
    bool enabled;
    int set_duty_calls;
    int set_phase_calls;
} mock_inverter_t;

static edge_status_t mock_set_duty(void *self, float duty_a, float duty_b, float duty_c) {
    mock_inverter_t *inv = (mock_inverter_t *)self;
    inv->duty_a = duty_a;
    inv->duty_b = duty_b;
    inv->duty_c = duty_c;
    inv->set_duty_calls++;
    return EDGE_OK;
}

static edge_status_t mock_set_phase_state(void *self, bool enable) {
    mock_inverter_t *inv = (mock_inverter_t *)self;
    inv->enabled = enable;
    inv->set_phase_calls++;
    return EDGE_OK;
}

/* Current sensor mock */
typedef struct mock_current_sensor {
    float ia;
    float ib;
    float ic;
    float v_bus;
} mock_current_sensor_t;

static edge_status_t mock_read_currents(void *self, float *ia, float *ib, float *ic) {
    mock_current_sensor_t *cs = (mock_current_sensor_t *)self;
    *ia = cs->ia;
    *ib = cs->ib;
    *ic = cs->ic;
    return EDGE_OK;
}

static edge_status_t mock_read_vbus(void *self, float *v_bus) {
    mock_current_sensor_t *cs = (mock_current_sensor_t *)self;
    *v_bus = cs->v_bus;
    return EDGE_OK;
}

/* Rotor sensor mock */
typedef struct mock_rotor_sensor {
    float angle_rad;
    float rpm;
} mock_rotor_sensor_t;

static edge_status_t mock_read_angle(void *self, float *angle_rad, float *rpm) {
    mock_rotor_sensor_t *rs = (mock_rotor_sensor_t *)self;
    *angle_rad = rs->angle_rad;
    *rpm = rs->rpm;
    return EDGE_OK;
}

/* Test 1: Math Clarke, Park, and Inverse Park Transforms */
static void test_foc_math_transforms(void **state) {
    (void)state;

    float ia = 10.0f, ib = -5.0f, ic = -5.0f;
    float i_alpha = 0.0f, i_beta = 0.0f;

    foc_clarke_transform(ia, ib, ic, &i_alpha, &i_beta);
    assert_float_equal(i_alpha, 10.0f, 0.001f);
    assert_float_equal(i_beta, 0.0f, 0.001f);

    float sin_th = 0.0f, cos_th = 1.0f; /* theta = 0 */
    float id = 0.0f, iq = 0.0f;
    foc_park_transform(i_alpha, i_beta, sin_th, cos_th, &id, &iq);
    assert_float_equal(id, 10.0f, 0.001f);
    assert_float_equal(iq, 0.0f, 0.001f);

    float v_alpha = 0.0f, v_beta = 0.0f;
    foc_inv_park_transform(id, iq, sin_th, cos_th, &v_alpha, &v_beta);
    assert_float_equal(v_alpha, 10.0f, 0.001f);
    assert_float_equal(v_beta, 0.0f, 0.001f);
}

/* Test 2: Space Vector Modulation (SVPWM) */
static void test_foc_math_svpwm(void **state) {
    (void)state;

    float v_bus = 24.0f;
    float da = 0.0f, db = 0.0f, dc = 0.0f;
    uint32_t sector = 0;

    /* Zero vector -> 50% duty */
    foc_svpwm(0.0f, 0.0f, v_bus, &da, &db, &dc, &sector);
    assert_float_equal(da, 0.5f, 0.01f);
    assert_float_equal(db, 0.5f, 0.01f);
    assert_float_equal(dc, 0.5f, 0.01f);

    /* Pure alpha positive voltage -> Sector 1 */
    foc_svpwm(10.0f, 0.0f, v_bus, &da, &db, &dc, &sector);
    assert_int_equal(sector, 1u);
    assert_true(da > db);
    assert_true(db >= dc);
}

/* Test 3: Module Construction and Contract */
static void test_foc_core_construct_contract(void **state) {
    (void)state;

    mock_inverter_t inv_mock = {0};
    foc_inverter_port_t inv_port = {
        .set_duty = mock_set_duty, .set_phase_state = mock_set_phase_state, .self = &inv_mock};

    mock_current_sensor_t cs_mock = {.v_bus = 24.0f};
    foc_current_port_t cs_port = {
        .read_currents = mock_read_currents, .read_vbus = mock_read_vbus, .self = &cs_mock};

    mock_rotor_sensor_t rs_mock = {0};
    foc_rotor_port_t rs_port = {.read_angle = mock_read_angle, .self = &rs_mock};

    foc_core_t foc;
    foc_core_construct(&foc, EDGE_MOD_FOC_CORE, 10u, (void *)0, &inv_port, &cs_port, &rs_port);

    assert_int_equal(foc.module.module_id, EDGE_MOD_FOC_CORE);
    assert_int_equal(foc.module.priority, 10u);
    assert_ptr_equal(foc_core_module(&foc), &foc.module);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_UNINITIALIZED);

    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_IDLE);

    assert_int_equal(foc_core_deinit(&foc), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_UNINITIALIZED);
}

/* Test 4: Fault Protection - Overvoltage and Undervoltage */
static void test_foc_core_voltage_protection(void **state) {
    (void)state;

    mock_inverter_t inv_mock = {0};
    foc_inverter_port_t inv_port = {
        .set_duty = mock_set_duty, .set_phase_state = mock_set_phase_state, .self = &inv_mock};

    mock_current_sensor_t cs_mock = {.v_bus = 24.0f};
    foc_current_port_t cs_port = {
        .read_currents = mock_read_currents, .read_vbus = mock_read_vbus, .self = &cs_mock};

    mock_rotor_sensor_t rs_mock = {0};
    foc_rotor_port_t rs_port = {.read_angle = mock_read_angle, .self = &rs_mock};

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 0.005f,
                        .pole_pairs = 7,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 100.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 100.0f,
                        .sensorless_mode = false};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    /* Enable current mode */
    assert_int_equal(foc_core_set_current(&foc, 10.0f, 0.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_CURRENT);

    /* Run normal loop */
    assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
    assert_true(inv_mock.enabled);

    /* Inject Overvoltage */
    cs_mock.v_bus = 65.0f;
    edge_status_t st = foc_core_fast_loop(&foc, 0.00005f);
    assert_int_equal(st, EDGE_EBUSY);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_FAULT);
    assert_true(foc_core_get_faults(&foc) & FOC_FAULT_OVER_VOLTAGE);
    assert_false(inv_mock.enabled);

    /* Clear faults after voltage normalized */
    cs_mock.v_bus = 24.0f;
    assert_int_equal(foc_core_clear_faults(&foc), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_IDLE);
}

/* Test 5: Thermal Protection via Background Step */
static void test_foc_core_thermal_protection(void **state) {
    (void)state;

    mock_inverter_t inv_mock = {0};
    foc_inverter_port_t inv_port = {
        .set_duty = mock_set_duty, .set_phase_state = mock_set_phase_state, .self = &inv_mock};
    mock_current_sensor_t cs_mock = {.v_bus = 24.0f};
    foc_current_port_t cs_port = {
        .read_currents = mock_read_currents, .read_vbus = mock_read_vbus, .self = &cs_mock};
    mock_rotor_sensor_t rs_mock = {0};
    foc_rotor_port_t rs_port = {.read_angle = mock_read_angle, .self = &rs_mock};

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 0.005f,
                        .pole_pairs = 7,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.1f,
                        .current_ki = 100.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 85.0f};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    foc_core_set_temperature(&foc, 90.0f);
    assert_int_equal(foc.module.poll(&foc.module), EDGE_OK);

    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_FAULT);
    assert_true(foc_core_get_faults(&foc) & FOC_FAULT_OVER_TEMP);
}

/* Virtual motor simulation context for closed loop test */
typedef struct sim_context {
    foc_virtual_motor_t vm;
    mock_inverter_t inv;
    float v_bus;
} sim_context_t;

static edge_status_t sim_set_duty(void *self, float duty_a, float duty_b, float duty_c) {
    sim_context_t *ctx = (sim_context_t *)self;
    ctx->inv.duty_a = duty_a;
    ctx->inv.duty_b = duty_b;
    ctx->inv.duty_c = duty_c;
    return EDGE_OK;
}

static edge_status_t sim_set_phase_state(void *self, bool enable) {
    sim_context_t *ctx = (sim_context_t *)self;
    ctx->inv.enabled = enable;
    return EDGE_OK;
}

static edge_status_t sim_read_currents(void *self, float *ia, float *ib, float *ic) {
    sim_context_t *ctx = (sim_context_t *)self;
    *ia = ctx->vm.ia;
    *ib = ctx->vm.ib;
    *ic = ctx->vm.ic;
    return EDGE_OK;
}

static edge_status_t sim_read_vbus(void *self, float *v_bus) {
    sim_context_t *ctx = (sim_context_t *)self;
    *v_bus = ctx->v_bus;
    return EDGE_OK;
}

static edge_status_t sim_read_angle(void *self, float *angle_rad, float *rpm) {
    sim_context_t *ctx = (sim_context_t *)self;
    *angle_rad = ctx->vm.rotor_angle_rad;
    *rpm = ctx->vm.rotor_speed_rad_s * 60.0f / (2.0f * (float)M_PI);
    return EDGE_OK;
}

/* Test 6: Closed-loop Current Control with Virtual Motor */
static void test_foc_core_closed_loop_virtual_motor(void **state) {
    (void)state;

    sim_context_t sim;
    sim.v_bus = 24.0f;
    sim.inv.enabled = false;
    foc_virtual_motor_init(&sim.vm, 0.05f, 0.00005f, 0.005f, 7, 0.0005f);

    foc_inverter_port_t inv_port = {
        .set_duty = sim_set_duty, .set_phase_state = sim_set_phase_state, .self = &sim};
    foc_current_port_t cs_port = {
        .read_currents = sim_read_currents, .read_vbus = sim_read_vbus, .self = &sim};
    foc_rotor_port_t rs_port = {.read_angle = sim_read_angle, .self = &sim};

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 0.005f,
                        .pole_pairs = 7,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.15f,
                        .current_ki = 300.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 100.0f,
                        .sensorless_mode = false};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    /* Command 8.0 Amps on q-axis */
    float target_iq = 8.0f;
    assert_int_equal(foc_core_set_current(&foc, target_iq, 0.0f), EDGE_OK);

    float dt = 0.00005f; /* 20 kHz loop */

    /* Run 2000 steps = 100ms simulation */
    for (int step = 0; step < 2000; step++) {
        /* 1. FOC fast control loop computes inverter duties */
        assert_int_equal(foc_core_fast_loop(&foc, dt), EDGE_OK);

        /* 2. Step physical virtual motor with FOC output voltages */
        foc_virtual_motor_step(&sim.vm, foc.v_alpha, foc.v_beta, 0.0f, dt,
                               0.05f /* 0.05 Nm load */);
    }

    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);

    /* Assert current converged to target +/- 0.5A */
    assert_float_equal(telem.current_q, target_iq, 0.5f);
    assert_float_equal(telem.current_d, 0.0f, 0.5f);
    /* Assert motor is rotating */
    assert_true(telem.speed_rpm > 100.0f);
}

/*
 * The averages are read-and-reset, and the mask decides which ones. Two things
 * follow that a "return a snapshot" implementation cannot do: a channel you did
 * not ask for keeps accumulating its window, and the reference's 0/0 is visible
 * when you read a channel twice with no samples in between.
 */
static void test_foc_core_averages_are_read_reset_and_masked(void **state) {
    (void)state;

    sim_context_t sim;
    sim.v_bus = 24.0f;
    sim.inv.enabled = false;
    foc_virtual_motor_init(&sim.vm, 0.05f, 0.00005f, 0.005f, 7, 0.0005f);

    foc_inverter_port_t inv_port = {
        .set_duty = sim_set_duty, .set_phase_state = sim_set_phase_state, .self = &sim};
    foc_current_port_t cs_port = {
        .read_currents = sim_read_currents, .read_vbus = sim_read_vbus, .self = &sim};
    foc_rotor_port_t rs_port = {.read_angle = sim_read_angle, .self = &sim};

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 0.005f,
                        .pole_pairs = 7,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.15f,
                        .current_ki = 300.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 100.0f,
                        .sensorless_mode = false};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    assert_int_equal(foc_core_set_current(&foc, 8.0f, 0.0f), EDGE_OK);

    const float dt = 0.00005f;
    for (int step = 0; step < 2000; step++) {
        assert_int_equal(foc_core_fast_loop(&foc, dt), EDGE_OK);
        foc_virtual_motor_step(&sim.vm, foc.v_alpha, foc.v_beta, 0.0f, dt, 0.05f);
        assert_int_equal(foc.module.poll(&foc.module), EDGE_OK); /* the sampler tick */
    }

    foc_averages_t avg;

    /* Ask for id only. Unrequested channels are neither filled nor consumed. */
    foc_core_read_reset_averages(&foc, FOC_AVG_ID, &avg);
    assert_false(isnan(avg.id));
    assert_float_equal(avg.iq, 0.0f, 1e-9f);

    /* iq kept its window, so its average is still available ... */
    foc_core_read_reset_averages(&foc, FOC_AVG_IQ, &avg);
    assert_false(isnan(avg.iq));
    assert_true(fabsf(avg.iq) > 1.0f);

    /* ... while id was consumed and has nothing new to average. */
    foc_core_read_reset_averages(&foc, FOC_AVG_ID, &avg);
    assert_true(isnan(avg.id));
}

/* Test 7: Speed and Position Control Modes */
static void test_foc_core_modes(void **state) {
    (void)state;

    mock_inverter_t inv_mock = {0};
    foc_inverter_port_t inv_port = {
        .set_duty = mock_set_duty, .set_phase_state = mock_set_phase_state, .self = &inv_mock};
    mock_current_sensor_t cs_mock = {.v_bus = 24.0f};
    foc_current_port_t cs_port = {
        .read_currents = mock_read_currents, .read_vbus = mock_read_vbus, .self = &cs_mock};
    mock_rotor_sensor_t rs_mock = {.angle_rad = 0.5f, .rpm = 500.0f};
    foc_rotor_port_t rs_port = {.read_angle = mock_read_angle, .self = &rs_mock};

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 0.005f,
                        .pole_pairs = 7,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.15f,
                        .current_ki = 300.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 100.0f};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    /* Test RPM mode */
    assert_int_equal(foc_core_set_rpm(&foc, 1000.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_RPM);
    assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);

    /* Test Pos mode */
    assert_int_equal(foc_core_set_pos(&foc, 90.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_POS);
    assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);

    /* Test Handbrake mode */
    assert_int_equal(foc_core_set_handbrake(&foc, 15.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_RUNNING_CURRENT);
    assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
}

/*
 * The reference firmware's util/utils_math.c utils_fast_sincos_better - a parabola
 * fit plus one 0.225 refinement pass, deliberately not exact. Golden values come
 * from running the reference; tolerance is well below the fit's residual error
 * (up to 0.0011), so replacing this with sinf/cosf fails the test.
 */
static void test_foc_math_sincos_matches_reference(void **state) {
    (void)state;
    static const struct {
        float angle;
        float sin_v;
        float cos_v;
    } golden[] = {
        {0.0f, 0.0f, 1.0f},
        {0.5235988f, 0.5f, 0.8666666f},
        {1.5707963f, 1.0f, 0.0000001f},
        {-2.0f, -0.9097958f, -0.4157479f},
        {2.5f, 0.5988864f, -0.8018935f},
    };

    for (size_t i = 0; i < sizeof golden / sizeof golden[0]; i++) {
        float s = 0.0f;
        float c = 0.0f;
        foc_fast_sincos(golden[i].angle, &s, &c);
        assert_float_equal(s, golden[i].sin_v, 1e-6f);
        assert_float_equal(c, golden[i].cos_v, 1e-6f);
    }
}

/*
 * Two guards the reference observer has and a naive rewrite drops: a NaN sample
 * must not poison x1/x2 permanently, and a flux vector that collapses towards
 * zero must be lifted, or atan2 jumps by half a turn on noise alone.
 *
 * Note the scope: the reference guards its state, not its inputs, so a NaN sample
 * still yields a NaN phase for that sample. What must hold is that the observer
 * recovers on the next valid sample.
 */
static void test_foc_observer_nan_and_flux_floor(void **state) {
    (void)state;
    foc_observer_t obs;

    foc_observer_init(&obs, 0.005f);
    foc_observer_update(&obs, NAN, NAN, NAN, NAN, 5e-5f, 0.05f, 5e-5f, 0.005f, 1000.0f);
    assert_false(isnan(obs.x1));
    assert_false(isnan(obs.x2));

    /* Still usable afterwards: one bad sample is not a latched failure. The phase
     * recovers on the first valid sample and the derived speed on the second, since
     * it is a difference against the previous (still poisoned) phase. */
    foc_observer_update(&obs, 1.0f, 0.0f, 0.1f, 0.0f, 5e-5f, 0.05f, 5e-5f, 0.005f, 1000.0f);
    assert_false(isnan(obs.phase));
    foc_observer_update(&obs, 1.0f, 0.0f, 0.1f, 0.0f, 5e-5f, 0.05f, 5e-5f, 0.005f, 1000.0f);
    assert_false(isnan(obs.phase));
    assert_false(isnan(obs.speed_rad_s));

    /* Start below half the configured linkage and let the floor lift the vector. */
    foc_observer_init(&obs, 0.001f);
    foc_observer_update(&obs, 0.0f, 0.0f, 0.0f, 0.0f, 5e-5f, 0.05f, 5e-5f, 0.005f, 0.0f);
    assert_float_equal(obs.x1, 0.001f * 1.1f, 1e-6f);
    assert_float_equal(obs.x2, 0.0f, 1e-6f);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_foc_math_transforms),
        cmocka_unit_test(test_foc_math_svpwm),
        cmocka_unit_test(test_foc_math_sincos_matches_reference),
        cmocka_unit_test(test_foc_observer_nan_and_flux_floor),
        cmocka_unit_test(test_foc_core_construct_contract),
        cmocka_unit_test(test_foc_core_voltage_protection),
        cmocka_unit_test(test_foc_core_thermal_protection),
        cmocka_unit_test(test_foc_core_closed_loop_virtual_motor),
        cmocka_unit_test(test_foc_core_averages_are_read_reset_and_masked),
        cmocka_unit_test(test_foc_core_modes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
