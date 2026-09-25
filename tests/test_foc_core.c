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
    foc_svpwm(0.0f, 0.0f, v_bus, 0.95f, &da, &db, &dc, &sector);
    assert_float_equal(da, 0.5f, 1e-5f);
    assert_float_equal(db, 0.5f, 1e-5f);
    assert_float_equal(dc, 0.5f, 1e-5f);

    /*
     * Pure alpha, reference arithmetic: mod = 1.5 * v / v_bus (mcpwm_foc.c:3808),
     * sector 1 -> t1 = mod, t2 = 0, da = (1 + t1 + t2)/2, db = da - t1, dc = db.
     */
    foc_svpwm(10.0f, 0.0f, v_bus, 0.95f, &da, &db, &dc, &sector);
    assert_int_equal(sector, 1u);
    float mod = 1.5f * 10.0f / v_bus;
    assert_float_equal(da, (1.0f + mod) * 0.5f, 1e-5f);
    assert_float_equal(db, (1.0f + mod) * 0.5f - mod, 1e-5f);
    assert_float_equal(dc, db, 1e-5f);

    /*
     * Saturation is a PER-PHASE clamp at
     * t_max = 1 - (1 - duty_max) * 0.5 (motor/foc_math.c:374), not a clamp on the
     * vector magnitude: the vector is left alone and the phases clip where they land.
     */
    foc_svpwm(20.0f, 0.0f, v_bus, 0.95f, &da, &db, &dc, &sector);
    float t_max = 1.0f - (1.0f - 0.95f) * 0.5f;
    assert_float_equal(da, t_max, 1e-5f);
    assert_float_equal(db, 0.0f, 1e-5f);
    assert_float_equal(dc, 0.0f, 1e-5f);
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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

/*
 * The duty the protocol reports - and the one foc_core_set_current_rel reads - is
 * not a phase duty. The reference derives it from the commanded voltages as
 *   duty_now = SIGN(vq) * NORM2_f(mod_d, mod_q) * p_duty_norm
 * (mcpwm_foc.c:3818-3820) with mod = v * 1.5 / v_bus and p_duty_norm = TWO_BY_SQRT3
 * for the default overmodulation factor of 1.0. In duty mode vq = d * v_bus * 2/3
 * and vd = 0, so mod_q collapses to exactly d and the expected value is d times
 * TWO_BY_SQRT3 - worked out from the reference's formula, not from the port.
 */
static void test_foc_core_duty_now_is_a_modulation_magnitude(void **state) {
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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

    foc_telemetry_t t;

    assert_int_equal(foc_core_set_duty(&foc, 0.5f), EDGE_OK);
    assert_int_equal(foc_core_fast_loop(&foc, 0.001f), EDGE_OK);
    foc_core_get_telemetry(&foc, &t);
    assert_float_equal(t.duty_now, 0.5f * TWO_BY_SQRT3, 1e-5f);

    /* Signed, not a magnitude: a reverse command reports a negative duty while the
     * phase duties stay non-negative. */
    assert_int_equal(foc_core_set_duty(&foc, -0.5f), EDGE_OK);
    assert_int_equal(foc_core_fast_loop(&foc, 0.001f), EDGE_OK);
    foc_core_get_telemetry(&foc, &t);
    assert_float_equal(t.duty_now, -0.5f * TWO_BY_SQRT3, 1e-5f);
    /* The phase duty (internal, not in the telemetry snapshot) stays non-negative. */
    assert_true(foc.duty_a >= 0.0f);
}

/*
 * COMM_SET_CURRENT_REL's limit base, reference mc_interface_set_current_rel
 * (mc_interface.c:733-749): the positive limit applies while the machine is near
 * standstill or when the setpoint pushes the same way as the duty, the negative one
 * otherwise. The two limits are asymmetric here on purpose - that is what makes the
 * chosen one visible in the result.
 */
static void test_foc_core_set_current_rel_picks_its_limit_from_the_duty(void **state) {
    (void)state;

    mock_inverter_t inv = {0};
    mock_current_sensor_t cs = {0};
    mock_rotor_sensor_t rs = {0};

    foc_inverter_port_t inv_port = {
        .set_duty = mock_set_duty, .set_phase_state = mock_set_phase_state, .self = &inv};
    foc_current_port_t cs_port = {
        .read_currents = mock_read_currents, .read_vbus = mock_read_vbus, .self = &cs};
    foc_rotor_port_t rs_port = {.read_angle = mock_read_angle, .self = &rs};

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 0.005f,
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
                        .current_max_a = 40.0f,
                        .current_min_a = -60.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.15f,
                        .current_ki = 300.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 100.0f,
                        .sensorless_mode = false};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    /* Near standstill (|duty| < 0.02) the positive limit applies whatever the sign. */
    foc.duty_now = 0.0f;
    assert_int_equal(foc_core_set_current_rel(&foc, -1.0f), EDGE_OK);
    assert_float_equal(foc.target_iq, -40.0f, 1e-5f);

    /* Braking against a forward duty takes the negative limit ... */
    foc.duty_now = 0.5f;
    assert_int_equal(foc_core_set_current_rel(&foc, -1.0f), EDGE_OK);
    assert_float_equal(foc.target_iq, -60.0f, 1e-5f);

    /* ... and driving with it takes the positive one. */
    assert_int_equal(foc_core_set_current_rel(&foc, 1.0f), EDGE_OK);
    assert_float_equal(foc.target_iq, 40.0f, 1e-5f);

    /* Mirrored for a reverse duty. */
    foc.duty_now = -0.5f;
    assert_int_equal(foc_core_set_current_rel(&foc, 1.0f), EDGE_OK);
    assert_float_equal(foc.target_iq, 60.0f, 1e-5f);

    assert_int_equal(foc_core_set_current_rel(&foc, -0.5f), EDGE_OK);
    assert_float_equal(foc.target_iq, -20.0f, 1e-5f);

    /* No clamp on this path, as on the absolute command. */
    assert_int_equal(foc_core_set_current_rel(&foc, 10.0f), EDGE_OK);
    assert_float_equal(foc.target_iq, 600.0f, 1e-4f);
}

/*
 * Handbrake forces the electrical phase to zero (mcpwm_foc.c:3602), which is what makes
 * it lock the rotor rather than drive it. The reported phase, the Park transform and the
 * sector-based tachometer all follow that angle, so a rotor sitting at 1 rad must be
 * reported at 0 once handbrake is entered.
 */
static void test_foc_core_handbrake_forces_the_phase_to_zero(void **state) {
    (void)state;

    mock_inverter_t inv = {0};
    mock_current_sensor_t cs = {.v_bus = 24.0f};
    mock_rotor_sensor_t rs = {.angle_rad = 1.0f, .rpm = 500.0f};

    foc_inverter_port_t inv_port = {
        .set_duty = mock_set_duty, .set_phase_state = mock_set_phase_state, .self = &inv};
    foc_current_port_t cs_port = {
        .read_currents = mock_read_currents, .read_vbus = mock_read_vbus, .self = &cs};
    foc_rotor_port_t rs_port = {.read_angle = mock_read_angle, .self = &rs};

    foc_core_t foc;
    foc_config_t cfg = {.r_ohm = 0.05f,
                        .l_henry = 0.00005f,
                        .lambda_wb = 0.005f,
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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

    /* Without handbrake the rotor angle is reported as the sensor gives it. */
    assert_int_equal(foc_core_set_current(&foc, 1.0f, 0.0f), EDGE_OK);
    assert_int_equal(foc_core_fast_loop(&foc, 0.001f), EDGE_OK);
    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);
    assert_float_equal(telem.rotor_angle_rad, 1.0f, 1e-6f);

    assert_int_equal(foc_core_set_handbrake(&foc, 15.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_HANDBRAKE);
    assert_int_equal(foc_core_fast_loop(&foc, 0.001f), EDGE_OK);
    foc_core_get_telemetry(&foc, &telem);
    assert_float_equal(telem.rotor_angle_rad, 0.0f, 1e-9f);
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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

/*
 * Energy counters. The reference gates them on the FILTERED motor current
 * magnitude (> 1 A) and splits powered/charged by the sign of the input current
 * (mc_interface.c:2036), so a current below the gate must accumulate nothing.
 */
static void test_foc_core_energy_counters(void **state) {
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.15f,
                        .current_ki = 300.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 100.0f,
                        .current_filter_const = 0.1f,
                        .sensorless_mode = false};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);

    foc_telemetry_t telem;

    /* Idle: below the 1 A gate, nothing accumulates and the bus current is 0. */
    for (int step = 0; step < 200; step++) {
        assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
        foc_virtual_motor_step(&sim.vm, foc.v_alpha, foc.v_beta, 0.0f, 0.00005f, 0.0f);
    }
    foc_core_get_telemetry(&foc, &telem);
    assert_float_equal(telem.amp_hours, 0.0f, 1e-9f);
    assert_float_equal(telem.watt_hours, 0.0f, 1e-9f);

    /* Drive a real current: the counters must start moving, in the same direction. */
    assert_int_equal(foc_core_set_current(&foc, 8.0f, 0.0f), EDGE_OK);
    for (int step = 0; step < 4000; step++) {
        assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
        foc_virtual_motor_step(&sim.vm, foc.v_alpha, foc.v_beta, 0.0f, 0.00005f, 0.05f);
    }

    foc_core_get_telemetry(&foc, &telem);
    assert_true(telem.amp_hours > 0.0f);
    assert_true(telem.watt_hours > 0.0f);
    assert_true(telem.current_in > 0.0f);
    /* Power balance: v_bus * current_in must equal 1.5 * (vd*id + vq*iq). */
    assert_float_equal(telem.v_bus * telem.current_in,
                       1.5f * (foc.v_d * foc.last_id + foc.v_q * foc.last_iq), 1e-2f);
    /* Nothing was regenerated, so the charged side stays empty. */
    assert_float_equal(telem.amp_hours_charged, 0.0f, 1e-9f);
    assert_float_equal(telem.watt_hours_charged, 0.0f, 1e-9f);

    /* Amp-hours are the integral of the bus current over the loop dt, so the
     * increment across N steps must equal the sum of i_bus * dt over those steps. */
    foc_telemetry_t before;
    foc_core_get_telemetry(&foc, &before);

    float expected_amp_seconds = 0.0f;
    for (int step = 0; step < 100; step++) {
        assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
        foc_virtual_motor_step(&sim.vm, foc.v_alpha, foc.v_beta, 0.0f, 0.00005f, 0.05f);
        expected_amp_seconds += foc.i_bus * 0.00005f;
    }

    foc_core_get_telemetry(&foc, &telem);
    assert_true(expected_amp_seconds > 0.0f);
    assert_float_equal((telem.amp_hours - before.amp_hours) * 3600.0f, expected_amp_seconds, 1e-3f);
}

/*
 * Statistics. Averages are sum/samples, maxima are running maxima since the last
 * reset, and the reference seeds the two temperature maxima at -300 so an
 * unupdated stat does not read as a plausible 0 C.
 */
static void test_foc_core_stats_and_reset(void **state) {
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
                        .current_max_a = 50.0f,
                        .current_min_a = -50.0f,
                        .duty_max = 0.95f,
                        .current_kp = 0.15f,
                        .current_ki = 300.0f,
                        .vbus_ov_threshold = 60.0f,
                        .vbus_uv_threshold = 12.0f,
                        .temp_fet_max_c = 100.0f,
                        .current_filter_const = 0.1f,
                        .sensorless_mode = false};

    foc_core_construct(&foc, 1u, 1u, &cfg, &inv_port, &cs_port, &rs_port);
    assert_int_equal(foc_core_init(&foc), EDGE_OK);
    foc_core_set_temperature(&foc, 30.0f);

    foc_stats_t st;

    /* Nothing sampled yet: averages are the reference's 0/0, maxima untouched. */
    foc_core_get_stats(&foc, &st);
    assert_true(isnan(st.power_avg));
    assert_float_equal(st.temp_mos_max, -300.0f, 1e-6f);
    assert_float_equal(st.temp_motor_max, -300.0f, 1e-6f);

    /* Sample with the motor running. */
    assert_int_equal(foc_core_set_current(&foc, 8.0f, 0.0f), EDGE_OK);
    for (int step = 0; step < 2000; step++) {
        assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
        foc_virtual_motor_step(&sim.vm, foc.v_alpha, foc.v_beta, 0.0f, 0.00005f, 0.05f);
        assert_int_equal(foc.module.poll(&foc.module), EDGE_OK);
    }

    foc_core_get_stats(&foc, &st);
    assert_false(isnan(st.speed_avg));
    assert_true(st.speed_avg > 0.0f);
    assert_true(st.speed_max >= st.speed_avg);
    /*
     * Reference speed scaling: mech_rpm/60 * wheel diameter * pi / gear ratio.
     * Checked on the instantaneous value; the average is a different number by
     * construction (the motor was accelerating).
     */
    assert_float_equal(
        foc.speed_m_s,
        (foc.last_rpm / 60.0f) * cfg.si_wheel_diameter * (float)M_PI / cfg.si_gear_ratio, 1e-5f);
    assert_true(st.speed_avg <= st.speed_max);
    assert_false(isnan(st.power_avg));
    assert_true(st.power_avg > 0.0f);
    assert_true(st.current_avg > 1.0f);
    assert_true(st.power_max >= st.power_avg);
    assert_true(st.current_max >= st.current_avg);
    assert_float_equal(st.temp_mos_avg, 30.0f, 1e-3f); /* constant FET temperature */
    assert_float_equal(st.temp_mos_max, 30.0f, 1e-3f);
    /* Feeding a hotter FET raises the maximum but not the average retroactively. */
    foc_core_set_temperature(&foc, 55.0f);
    assert_int_equal(foc.module.poll(&foc.module), EDGE_OK);
    foc_core_get_stats(&foc, &st);
    assert_float_equal(st.temp_mos_max, 55.0f, 1e-3f);
    assert_true(st.temp_mos_avg < 55.0f);

    /* Reset puts everything back, temperature maxima included. */
    foc_core_stats_reset(&foc);
    foc_core_get_stats(&foc, &st);
    assert_true(isnan(st.power_avg));
    assert_float_equal(st.power_max, 0.0f, 1e-9f);
    assert_float_equal(st.current_max, 0.0f, 1e-9f);
    assert_float_equal(st.temp_mos_max, -300.0f, 1e-6f);
    assert_float_equal(st.temp_motor_max, -300.0f, 1e-6f);
}

/*
 * Tachometer. Counted from the phase the FOC already has, in six 60-degree
 * sectors (reference mcpwm_foc.c:3866), so a full forward revolution is six
 * steps and the 5 -> 0 wrap is one step forward rather than five back.
 */
static void test_foc_core_tachometer_sectors(void **state) {
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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
    foc_core_set_temperature(&foc, 25.0f);

    /*
     * One angle in the middle of each sector: sector s spans
     * [-pi + s*2pi/6, -pi + (s+1)*2pi/6), so the centre is -pi + (s+0.5)*2pi/6.
     */
    const float sector_centre[6] = {-2.618f, -1.571f, -0.524f, 0.524f, 1.571f, 2.618f};
    for (int i = 0; i < 6; i++) {
        rs_mock.angle_rad = sector_centre[i];
        assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
    }
    /* and one more sample crossing 5 -> 0, which must count as +1, not -5 */
    rs_mock.angle_rad = sector_centre[0];
    assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);

    foc_telemetry_t telem;
    foc_core_get_telemetry(&foc, &telem);
    assert_int_equal(telem.tachometer, 6);
    assert_int_equal(telem.tachometer_abs, 6);

    /* Reverse through the same sectors: sign flips, absolute value keeps counting. */
    for (int i = 5; i >= 0; i--) {
        rs_mock.angle_rad = sector_centre[i];
        assert_int_equal(foc_core_fast_loop(&foc, 0.00005f), EDGE_OK);
    }
    foc_core_get_telemetry(&foc, &telem);
    assert_int_equal(telem.tachometer, 0);
    assert_int_equal(telem.tachometer_abs, 12);
}

/*
 * The observer family. The differential harness against the reference's
 * motor/foc_math.c is the real proof (all seven types bit-identical over 4000
 * steps); this keeps the port's own suite honest about two things the harness
 * cannot cover from here: that the selector actually selects, and that every
 * type stays inside the reference's flux clamp.
 */
static void test_foc_observer_family(void **state) {
    (void)state;
    const float lambda = 0.00245f;
    const float dt = 5e-5f;
    float first_x1[7];
    float first_phase[7];

    for (int type = 0; type < 7; type++) {
        foc_observer_t obs;
        foc_observer_init(&obs, lambda);

        for (int n = 0; n < 500; n++) {
            float t = (float)n * dt;
            float ia = 5.0f * sinf(2.0f * (float)M_PI * 60.0f * t);
            float ib = 5.0f * cosf(2.0f * (float)M_PI * 60.0f * t);
            float va = 3.0f * cosf(2.0f * (float)M_PI * 60.0f * t);
            float vb = 3.0f * sinf(2.0f * (float)M_PI * 60.0f * t);
            foc_observer_update(&obs, va, vb, ia, ib, dt, 0.015f, 7e-6f, lambda, 9.0e5f,
                                (foc_observer_type_t)type);
        }

        /*
         * What the reference actually guarantees, per type: the *_LAMBDA_COMP
         * variants clamp their flux estimate into [0.3, 2.5] * lambda; the plain
         * Ortega and MXV branches have no state clamp at all, so asserting one for
         * them would be inventing a property the reference does not have.
         */
        assert_false(isnan(obs.phase));
        assert_false(isnan(obs.x1));
        assert_false(isnan(obs.x2));
        assert_false(isnan(obs.lambda_est));

        bool clamps_lambda = (type == FOC_OBSERVER_ORTEGA_LAMBDA_COMP) ||
                             (type == FOC_OBSERVER_MXLEMMING_LAMBDA_COMP) ||
                             (type == FOC_OBSERVER_MXV_LAMBDA_COMP) ||
                             (type == FOC_OBSERVER_MXV_LAMBDA_COMP_LIN);
        if (clamps_lambda) {
            assert_true(obs.lambda_est >= lambda * 0.3f - 1e-9f);
            assert_true(obs.lambda_est <= lambda * 2.5f + 1e-9f);
        }
        first_x1[type] = obs.x1;
        first_phase[type] = obs.phase;
    }

    /* Selecting a different algorithm must produce a different state, or the
     * selector is decorative. */
    assert_true(fabsf(first_x1[0] - first_x1[1]) > 1e-6f);
    assert_true(fabsf(first_phase[0] - first_phase[1]) > 1e-6f);
}

/*
 * PLL (reference foc_math.c:225 foc_pll_run). It tracks the phase it is fed and
 * its speed output is the electrical speed the control path uses; with a phase
 * advancing at a constant rate the speed must settle on that rate.
 */
static void test_foc_pll_tracks_phase_rate(void **state) {
    (void)state;
    foc_pll_t pll = {.phase = 0.0f, .speed = 0.0f};

    const float dt = 5e-5f;
    const float rate = 2.0f * (float)M_PI * 100.0f; /* 100 Hz electrical */
    float phase = 0.0f;

    for (int n = 0; n < 20000; n++) {
        phase += rate * dt;
        while (phase >= (float)M_PI) {
            phase -= 2.0f * (float)M_PI;
        }
        foc_pll_run(&pll, phase, dt, 2000.0f, 30000.0f);
    }

    assert_float_equal(pll.speed, rate, rate * 0.02f);
    assert_false(isnan(pll.phase));
    assert_true(fabsf(pll.phase) <= (float)M_PI);

    /* A NaN on either state is cleared rather than latched. */
    pll.phase = NAN;
    pll.speed = NAN;
    foc_pll_run(&pll, 0.5f, dt, 2000.0f, 30000.0f);
    assert_false(isnan(pll.phase));
    assert_false(isnan(pll.speed));
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
                        .si_motor_poles = 14u,
                        .si_gear_ratio = 3.0f,
                        .si_wheel_diameter = 0.083f,
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

    /* Test Handbrake mode: a mode of its own, with the setpoint on the q axis. The port
     * used to write the d axis and return RUNNING_CURRENT, which made it a plain current
     * command that happened to brake. */
    assert_int_equal(foc_core_set_handbrake(&foc, 15.0f), EDGE_OK);
    assert_int_equal(foc_core_get_state(&foc), FOC_STATE_HANDBRAKE);
    assert_float_equal(foc.target_iq, 15.0f, 1e-6f);
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
    foc_observer_update(&obs, NAN, NAN, NAN, NAN, 5e-5f, 0.05f, 5e-5f, 0.005f, 1000.0f,
                        FOC_OBSERVER_ORTEGA_ORIGINAL);
    assert_false(isnan(obs.x1));
    assert_false(isnan(obs.x2));

    /* Still usable afterwards: one bad sample is not a latched failure. */
    foc_observer_update(&obs, 1.0f, 0.0f, 0.1f, 0.0f, 5e-5f, 0.05f, 5e-5f, 0.005f, 1000.0f,
                        FOC_OBSERVER_ORTEGA_ORIGINAL);
    assert_false(isnan(obs.phase));
    foc_observer_update(&obs, 1.0f, 0.0f, 0.1f, 0.0f, 5e-5f, 0.05f, 5e-5f, 0.005f, 1000.0f,
                        FOC_OBSERVER_ORTEGA_ORIGINAL);
    assert_false(isnan(obs.phase));

    /* Start below half the configured linkage and let the floor lift the vector. */
    foc_observer_init(&obs, 0.001f);
    foc_observer_update(&obs, 0.0f, 0.0f, 0.0f, 0.0f, 5e-5f, 0.05f, 5e-5f, 0.005f, 0.0f,
                        FOC_OBSERVER_ORTEGA_ORIGINAL);
    assert_float_equal(obs.x1, 0.001f * 1.1f, 1e-6f);
    assert_float_equal(obs.x2, 0.0f, 1e-6f);
}

/*
 * Parameter compensation. The differential harness proves the whole chain against
 * the reference (four combinations of saturation mode and temperature
 * compensation, bit-exact); these assertions keep each rule individually visible,
 * so a change to one of them fails by name rather than inside a blob comparison.
 */
static void test_foc_observer_adjust_params(void **state) {
    (void)state;
    const float r = 0.015f, l = 7e-6f, lambda = 0.00245f;
    float ra = 0.0f, la = 0.0f, lwa = 0.0f;

    /* Disabled: parameters come back untouched. */
    foc_observer_adjust_params(r, l, lambda, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, lambda, 0.0f,
                               FOC_SAT_COMP_DISABLED, 0.0f, false, FOC_OBSERVER_ORTEGA_ORIGINAL,
                               &ra, &la, &lwa);
    assert_float_equal(ra, r, 1e-9f);
    assert_float_equal(la, l, 1e-9f);
    assert_float_equal(lwa, lambda, 1e-9f);

    /* FACTOR: L and lambda shrink by sat_comp * (i_abs_filter / l_current_max). */
    float fact = 0.25f * (30.0f / 60.0f);
    foc_observer_adjust_params(r, l, lambda, 0.0f, 0.0f, 0.0f, 30.0f, 60.0f, lambda, 0.25f,
                               FOC_SAT_COMP_FACTOR, 0.0f, false, FOC_OBSERVER_ORTEGA_ORIGINAL, &ra,
                               &la, &lwa);
    assert_float_equal(la, l - l * fact, 1e-12f);
    assert_float_equal(lwa, lambda - lambda * fact, 1e-12f);
    assert_float_equal(ra, r, 1e-9f); /* this branch leaves R alone */

    /* Temperature compensation replaces R with the model's value. */
    foc_observer_adjust_params(r, l, lambda, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, lambda, 0.0f,
                               FOC_SAT_COMP_DISABLED, 0.021f, true, FOC_OBSERVER_ORTEGA_ORIGINAL,
                               &ra, &la, &lwa);
    assert_float_equal(ra, 0.021f, 1e-9f);

    /* Saliency moves L by ld_lq_diff's projection on iq, and only above the
     * reference's 0.1 A gate. */
    float ld_lq = 2.5e-6f, id = 4.0f, iq = 6.0f;
    foc_observer_adjust_params(r, l, lambda, ld_lq, id, iq, 0.0f, 60.0f, lambda, 0.0f,
                               FOC_SAT_COMP_DISABLED, 0.0f, false, FOC_OBSERVER_ORTEGA_ORIGINAL,
                               &ra, &la, &lwa);
    assert_float_equal(la, l - ld_lq / 2.0f + ld_lq * (iq * iq) / (id * id + iq * iq), 1e-12f);
    foc_observer_adjust_params(r, l, lambda, ld_lq, 0.05f, 0.05f, 0.0f, 60.0f, lambda, 0.0f,
                               FOC_SAT_COMP_DISABLED, 0.0f, false, FOC_OBSERVER_ORTEGA_ORIGINAL,
                               &ra, &la, &lwa);
    assert_float_equal(la, l, 1e-12f);

    /* LAMBDA scales L by the live flux estimate, for the observers that track one. */
    float l_est = lambda * 0.6f;
    foc_observer_adjust_params(r, l, lambda, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, l_est, 0.0f,
                               FOC_SAT_COMP_LAMBDA, 0.0f, false, FOC_OBSERVER_MXV, &ra, &la, &lwa);
    assert_float_equal(la, l * (l_est / lambda), 1e-12f);
    foc_observer_adjust_params(r, l, lambda, 0.0f, 0.0f, 0.0f, 0.0f, 60.0f, l_est, 0.0f,
                               FOC_SAT_COMP_LAMBDA, 0.0f, false, FOC_OBSERVER_ORTEGA_ORIGINAL, &ra,
                               &la, &lwa);
    assert_float_equal(la, l, 1e-12f);
}

/*
 * Speed PID. The differential harness proves it against the reference's
 * foc_run_pid_control_speed bit-exactly (four configurations, comparing the iq
 * setpoint and the ramped internal setpoint every step); this keeps the release
 * path, the ramp rate and the braking clamp individually visible.
 */
static void test_foc_run_pid_speed(void **state) {
    (void)state;
    foc_speed_pid_t pid = {0};
    foc_speed_pid_params_t p = {.kp = 0.0048f,
                                .ki = 0.02f,
                                .kd = 0.0001f,
                                .kd_filter = 0.1f,
                                .ramp_erpms_s = 1000.0f,
                                .min_erpm = 100.0f,
                                .openloop_rpm = 700.0f,
                                .l_min_erpm = -100000.0f,
                                .l_max_erpm = 100000.0f,
                                .lo_current_max = 60.0f,
                                .current_max_scale = 1.0f,
                                .allow_braking = false,
                                .invert_direction = false};
    float iq = 0.0f;

    /* Below the minimum setpoint the loop releases the motor. */
    foc_run_pid_speed(&pid, &p, true, true, 0.0f, 50.0f, 0.001f, &iq);
    assert_float_equal(iq, 0.0f, 1e-9f);

    /* Not in speed mode: the setpoint is left alone, the loop state is cleared. */
    iq = 12.0f;
    pid.set_rpm = 500.0f;
    pid.i_term = 0.3f;
    foc_run_pid_speed(&pid, &p, false, true, 0.0f, 0.0f, 0.001f, &iq);
    assert_float_equal(iq, 12.0f, 1e-9f);
    assert_float_equal(pid.i_term, 0.0f, 1e-9f);

    /* The setpoint ramps toward the command at ramp_erpms_s * dt. */
    pid.set_rpm = 0.0f;
    foc_run_pid_speed(&pid, &p, true, true, 0.0f, 2000.0f, 0.001f, &iq);
    assert_float_equal(pid.set_rpm, 1.0f, 1e-5f);

    /* Braking disabled: a negative output while spinning forward is refused. */
    pid.set_rpm = 700.0f;
    pid.i_term = 0.0f;
    pid.prev_error = 0.0f;
    pid.d_filter = 0.0f;
    foc_run_pid_speed(&pid, &p, true, true, 1500.0f, 700.0f, 0.001f, &iq);
    assert_float_equal(iq, 0.0f, 1e-9f);

    /* Same command with braking allowed produces a negative (braking) setpoint. */
    p.allow_braking = true;
    pid.i_term = 0.0f;
    pid.prev_error = 0.0f;
    pid.d_filter = 0.0f;
    foc_run_pid_speed(&pid, &p, true, true, 1500.0f, 700.0f, 0.001f, &iq);
    assert_true(iq < 0.0f);
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
        cmocka_unit_test(test_foc_core_energy_counters),
        cmocka_unit_test(test_foc_core_stats_and_reset),
        cmocka_unit_test(test_foc_core_tachometer_sectors),
        cmocka_unit_test(test_foc_observer_family),
        cmocka_unit_test(test_foc_observer_adjust_params),
        cmocka_unit_test(test_foc_pll_tracks_phase_rate),
        cmocka_unit_test(test_foc_run_pid_speed),
        cmocka_unit_test(test_foc_core_modes),
        cmocka_unit_test(test_foc_core_duty_now_is_a_modulation_magnitude),
        cmocka_unit_test(test_foc_core_set_current_rel_picks_its_limit_from_the_duty),
        cmocka_unit_test(test_foc_core_handbrake_forces_the_phase_to_zero),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
