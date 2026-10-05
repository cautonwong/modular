/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* The double literal the reference uses, as this repository's FOC core defines it. */
#define M_PI 3.14159265358979323846

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "edge/modules.h"
#include "motor_id/motor_id.h"

/*
 * A plant that behaves the way the reference's control loop does while a measurement runs. The
 * test drives it explicitly, one control cycle per millisecond of procedure, because in the
 * reference those two run at different rates: the ISR keeps filling the accumulator while the
 * procedure sleeps. What the module is held to is the procedure - its phases, their timings, the
 * ramp rate, what it does with the accumulator, and what it does on a fault.
 */
typedef struct mock_plant {
    float r_ohm;
    uint32_t fault;
    bool accumulate; /* false models a control loop that never fills the accumulator */

    bool phase_override;
    float phase_override_rad;
    float iq_set;

    float i_sum;
    float v_sum;
    uint32_t samples;

    int reset_calls;
    int stop_calls;
} mock_plant_t;

/* One control cycle with the setpoint the procedure left behind: the current is what was asked for
 * and the voltage is what the resistance demands for it. The accumulator holds vector magnitudes.
 */
static void mock_control_cycle(mock_plant_t *plant) {
    if (!plant->accumulate) {
        return;
    }
    plant->i_sum += fabsf(plant->iq_set);
    plant->v_sum += fabsf(plant->iq_set * plant->r_ohm);
    plant->samples++;
}

static edge_status_t mock_set_phase_override(void *self, float angle_rad, bool enable) {
    mock_plant_t *plant = (mock_plant_t *)self;
    plant->phase_override = enable;
    plant->phase_override_rad = angle_rad;
    return EDGE_OK;
}

static edge_status_t mock_set_current(void *self, float iq) {
    ((mock_plant_t *)self)->iq_set = iq;
    return EDGE_OK;
}

static edge_status_t mock_reset_samples(void *self) {
    mock_plant_t *plant = (mock_plant_t *)self;
    plant->i_sum = 0.0f;
    plant->v_sum = 0.0f;
    plant->samples = 0u;
    plant->reset_calls++;
    return EDGE_OK;
}

static edge_status_t mock_read_samples(void *self, float *i_sum, float *v_sum, uint32_t *count) {
    mock_plant_t *plant = (mock_plant_t *)self;
    if (i_sum != NULL) {
        *i_sum = plant->i_sum;
    }
    if (v_sum != NULL) {
        *v_sum = plant->v_sum;
    }
    if (count != NULL) {
        *count = plant->samples;
    }
    return EDGE_OK;
}

static uint32_t mock_get_fault(void *self) {
    return ((mock_plant_t *)self)->fault;
}

static edge_status_t mock_stop(void *self) {
    mock_plant_t *plant = (mock_plant_t *)self;
    plant->stop_calls++;
    plant->iq_set = 0.0f;
    return EDGE_OK;
}

static motor_id_measure_port_t make_port(mock_plant_t *plant) {
    motor_id_measure_port_t port = {.self = plant,
                                    .set_phase_override = mock_set_phase_override,
                                    .set_current = mock_set_current,
                                    .reset_samples = mock_reset_samples,
                                    .read_samples = mock_read_samples,
                                    .get_fault = mock_get_fault,
                                    .stop = mock_stop};
    return port;
}

/* Advance the procedure by one millisecond with a control cycle alongside it. */
static void run_one_ms(motor_id_app_t *app, mock_plant_t *plant) {
    mock_control_cycle(plant);
    assert_int_equal(motor_id_step(app, 0.001f), EDGE_OK);
}

static void test_motor_id_guards_and_unported_procedures(void **state) {
    (void)state;
    mock_plant_t plant = {.r_ohm = 0.05f, .accumulate = true};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t app;

    /* Constructed with no port: init refuses, because every callback the procedure uses matters. */
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, NULL);
    assert_int_equal(motor_id_init(&app), EDGE_EINVAL);

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(app.state, MOTOR_ID_STATE_IDLE);

    /* Argument guards. */
    assert_int_equal(motor_id_measure_resistance(NULL, 1.0f, 10u, true), EDGE_EINVAL);
    assert_int_equal(motor_id_measure_resistance(&app, 1.0f, 0u, true), EDGE_EINVAL);
    assert_int_equal(motor_id_step(NULL, 0.001f), EDGE_EINVAL);
    assert_int_equal(motor_id_step(&app, 0.0f), EDGE_EINVAL);
    assert_ptr_equal(motor_id_get_result(NULL), NULL);
    assert_ptr_equal(motor_id_module(NULL), NULL);
    assert_int_equal(motor_id_get_fault(NULL), 0u);

    /* A missing callback is refused rather than quietly skipping that step. */
    motor_id_measure_port_t partial = port;
    partial.get_fault = NULL;
    motor_id_app_t partial_app;
    motor_id_construct(&partial_app, EDGE_MOD_MOTOR_ID, 40u, &partial);
    assert_int_equal(motor_id_init(&partial_app), EDGE_EINVAL);
    assert_int_equal(motor_id_measure_resistance(&partial_app, 1.0f, 10u, true), EDGE_EINVAL);

    /*
     * The composed measurements refuse a port that cannot drive them, and this mock has none of the
     * HFI callbacks the resistance-and-inductance sequence measures through. The flux-linkage
     * procedure that is not ported says so rather than inventing a measurement. The hall detection
     * has no stub left to assert: its own three pieces are ported and tested above, and the
     * procedure that runs them is the piece this phase still owes.
     */
    assert_int_equal(motor_id_measure_r_l(&app, 50.0f), EDGE_EINVAL);
    assert_int_equal(motor_id_measure_flux_linkage(&app), EDGE_ENOTSUP);
    assert_int_equal(motor_id_measure_r_l(NULL, 50.0f), EDGE_EINVAL);
    assert_int_equal(motor_id_measure_r_l(&app, 0.0f), EDGE_EINVAL);
}

/*
 * The ramp rate is the reference's: one step of |current|/200 per millisecond (:1819), so 0.5 A
 * takes 200 ms. 199 of them leave the setpoint one step short and the phase still ramping; the
 * 200th reaches the target exactly, and only the millisecond after that moves on.
 */
static void test_motor_id_ramp_rate_is_the_reference_timing(void **state) {
    (void)state;
    mock_plant_t plant = {.r_ohm = 0.05f, .accumulate = true};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_measure_resistance(&app, 0.5f, 10u, true), EDGE_OK);

    assert_int_equal(motor_id_step(&app, 0.199f), EDGE_OK);
    assert_int_equal(app.state, MOTOR_ID_STATE_RAMP);
    assert_float_equal(plant.iq_set, 0.5f - 0.5f / 200.0f, 1e-6f);

    /* The 200th millisecond reaches the target exactly, and the settle starts in that same
     * millisecond: the reference's loop re-tests its condition without sleeping again. */
    assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    assert_float_equal(plant.iq_set, 0.5f, 1e-6f);
    assert_int_equal(app.state, MOTOR_ID_STATE_SETTLE);

    /* Feeding exactly the same time in one call lands in the same place. */
    mock_plant_t coarse_plant = {.r_ohm = 0.05f, .accumulate = true};
    motor_id_measure_port_t coarse_port = make_port(&coarse_plant);
    motor_id_app_t coarse_app;
    motor_id_construct(&coarse_app, EDGE_MOD_MOTOR_ID, 40u, &coarse_port);
    assert_int_equal(motor_id_init(&coarse_app), EDGE_OK);
    assert_int_equal(motor_id_measure_resistance(&coarse_app, 0.5f, 10u, true), EDGE_OK);
    assert_int_equal(motor_id_step(&coarse_app, 0.200f), EDGE_OK);
    assert_int_equal(coarse_app.state, app.state);
    assert_float_equal(coarse_plant.iq_set, plant.iq_set, 1e-6f);
}

/*
 * The measurement itself, over a plant whose resistance is known: ramp the current up, wait the
 * reference's 50 ms, clear the accumulator once and only then, wait for the requested number of
 * control cycles, and divide the two averages in the reference's order.
 */
static void test_motor_id_measures_a_known_resistance(void **state) {
    (void)state;
    mock_plant_t plant = {.r_ohm = 0.056f, .accumulate = true};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);

    assert_int_equal(motor_id_measure_resistance(&app, 2.0f, 200u, true), EDGE_OK);

    /* Running again while it is measuring is refused. */
    assert_int_equal(motor_id_measure_resistance(&app, 1.0f, 10u, true), EDGE_EBUSY);

    int ms = 0;
    while (app.state != MOTOR_ID_STATE_COMPLETE) {
        run_one_ms(&app, &plant);
        assert_true(app.state != MOTOR_ID_STATE_FAILED);
        ms++;
        assert_true(ms < 20000);
    }

    /* 200 ms of ramp, 50 ms of settle, then one millisecond per sample. */
    assert_int_equal(ms, 200 + 50 + 200);
    assert_int_equal(plant.reset_calls, 1);
    assert_int_equal(plant.samples, 200u);

    const motor_id_result_t *result = motor_id_get_result(&app);
    assert_non_null(result);
    assert_true(result->valid);
    assert_float_equal(result->r_ohm, 0.056f, 1e-5f);
    assert_int_equal(motor_id_get_fault(&app), 0u);

    /* stop_after was set, so the motor was stopped and the setpoint zeroed. */
    assert_int_equal(plant.stop_calls, 1);
    assert_float_equal(plant.iq_set, 0.0f, 1e-9f);
    /* The phase was released with it. */
    assert_false(plant.phase_override);
}

/* Without stop_after the motor is left running with the phase held, which is what the reference's
 * composed R-then-L sequence relies on between its two passes (:1874). */
static void test_motor_id_keeps_the_motor_running_when_asked(void **state) {
    (void)state;
    mock_plant_t plant = {.r_ohm = 0.04f, .accumulate = true};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_measure_resistance(&app, 1.0f, 10u, false), EDGE_OK);

    for (int ms = 0; ms < 20000 && app.state != MOTOR_ID_STATE_COMPLETE; ms++) {
        run_one_ms(&app, &plant);
    }

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_true(motor_id_get_result(&app)->valid);
    assert_float_equal(motor_id_get_result(&app)->r_ohm, 0.04f, 1e-5f);
    assert_int_equal(plant.stop_calls, 0);
    assert_true(plant.phase_override);
}

/*
 * A fault aborts the procedure wherever it happens and stops the motor, which is what both fault
 * paths in the reference do (:1822-1833 while ramping, :1852-1863 while sampling).
 */
static void test_motor_id_aborts_on_a_fault(void **state) {
    (void)state;

    /* During the ramp. */
    mock_plant_t plant = {.r_ohm = 0.05f, .accumulate = true};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_measure_resistance(&app, 2.0f, 200u, true), EDGE_OK);

    for (int i = 0; i < 10; i++) {
        run_one_ms(&app, &plant);
    }
    assert_int_equal(app.state, MOTOR_ID_STATE_RAMP);
    plant.fault = 42u;
    run_one_ms(&app, &plant);

    assert_int_equal(app.state, MOTOR_ID_STATE_FAILED);
    assert_int_equal(motor_id_get_fault(&app), 42u);
    assert_false(motor_id_get_result(&app)->valid);
    assert_int_equal(plant.stop_calls, 1);
    assert_false(plant.phase_override);

    /* During the sampling wait. */
    mock_plant_t late_plant = {.r_ohm = 0.05f, .accumulate = false};
    motor_id_measure_port_t late_port = make_port(&late_plant);
    motor_id_app_t late_app;
    motor_id_construct(&late_app, EDGE_MOD_MOTOR_ID, 40u, &late_port);
    assert_int_equal(motor_id_init(&late_app), EDGE_OK);
    assert_int_equal(motor_id_measure_resistance(&late_app, 2.0f, 200u, true), EDGE_OK);

    for (int i = 0; i < 260; i++) {
        run_one_ms(&late_app, &late_plant);
    }
    assert_int_equal(late_app.state, MOTOR_ID_STATE_SAMPLE);
    late_plant.fault = 7u;
    run_one_ms(&late_app, &late_plant);

    assert_int_equal(late_app.state, MOTOR_ID_STATE_FAILED);
    assert_int_equal(motor_id_get_fault(&late_app), 7u);
    assert_int_equal(late_plant.stop_calls, 1);
}

/*
 * When the accumulator never fills, the reference's wait expires after 10000 ms and it publishes
 * whatever it has - which divides by zero. The number is computed the same way here, but a result
 * with no samples behind it is marked invalid rather than left for the caller to notice.
 */
static void test_motor_id_sample_timeout_publishes_an_invalid_result(void **state) {
    (void)state;
    mock_plant_t plant = {.r_ohm = 0.05f, .accumulate = false};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_measure_resistance(&app, 1.0f, 10u, true), EDGE_OK);

    int ms = 0;
    while (app.state != MOTOR_ID_STATE_COMPLETE) {
        run_one_ms(&app, &plant);
        ms++;
        assert_true(ms < 20000);
    }

    assert_int_equal(ms, 200 + 50 + 10001);
    assert_false(motor_id_get_result(&app)->valid);
    assert_int_equal(plant.stop_calls, 1);
}

/*
 * The module's own hooks, the construct guard and the accessors' guards. The hooks are what a
 * scheduler drives, and power_off has to leave the procedure's state where a restart can begin -
 * a motor left under a forced phase override after a shutdown would be worse than a stopped one.
 */
static void test_motor_id_hooks_and_accessor_guards(void **state) {
    (void)state;
    mock_plant_t plant = {.r_ohm = 0.05f, .accumulate = true};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t app;

    motor_id_construct(NULL, EDGE_MOD_MOTOR_ID, 40u, &port);
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);

    /* The hooks a scheduler drives. */
    assert_int_equal(app.module.poll(&app.module), EDGE_OK);
    assert_int_equal(app.module.on_event(&app.module, NULL), EDGE_OK);
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_int_equal(app.state, MOTOR_ID_STATE_IDLE);
    assert_int_equal(motor_id_get_fault(&app), 0u);

    /* Start something and power off in the middle of it: the override must be released. */
    assert_int_equal(motor_id_measure_resistance(&app, 2.0f, 200u, true), EDGE_OK);
    for (int i = 0; i < 50; i++) {
        run_one_ms(&app, &plant);
    }
    assert_true(plant.phase_override);
    assert_int_equal(app.module.power_off(&app.module), EDGE_OK);
    assert_false(plant.phase_override);
    assert_int_equal(app.state, MOTOR_ID_STATE_IDLE);

    /* The accessors' guards. */
    assert_int_equal(motor_id_get_fault(NULL), 0u);
    assert_ptr_equal(motor_id_get_result(NULL), NULL);
    assert_ptr_equal(motor_id_module(NULL), NULL);
}

/*
 * The flux-linkage procedure (conf_general.c:967-1316), phase by phase. The plant here is what the
 * reference watches: a duty that rises as the open-loop speed does until the target is reached - at
 * which point the procedure averages what it drove with, stops the phases and measures again - and
 * the three ways it gives up. Every expected number is the reference's own arithmetic.
 */
typedef enum mock_duty_mode {
    MOCK_DUTY_RISES, /* duty = rpm / 2000, so 0.5 is reached at 1000 rpm */
    MOCK_DUTY_FLAT,  /* duty never moves */
    MOCK_DUTY_DROPS  /* rises to 0.3 and then falls, which is the -2 exit */
} mock_duty_mode_t;

typedef struct mock_flux_plant {
    mock_duty_mode_t mode;
    float baseline_duty; /* what the motor draws standing still, before the ramp */
    float spinup_duty;   /* and what it draws while spinning up, in the flat mode */
    uint32_t fault_after_openloop_calls;
    bool config_entered;
    int enter_calls;
    int leave_calls;
    float kp_in;
    float ki_in;
    int openloop_calls;
    float last_openloop_current;
    float last_openloop_rpm;
    float v_q;
    float rad_s;
    /* Set when the procedure stops the motor. Afterwards the fixture reports a duty at the bottom
     * of the range, which is what a coasting motor looks like - and what the undriven measurement's
     * 0.02 guard is there to select. */
    bool stopped;
    /* The baseline phase reads the duty a thousand times before the spin-up asks for it, and those
     * thousand are the standstill measurement; the reads after them belong to the drive. */
    int duty_reads;
} mock_flux_plant_t;

static float mock_flux_duty(const mock_flux_plant_t *p) {
    if (p->stopped) {
        return 0.01f;
    }
    /* While the baseline is being measured - the first thousand reads - the motor is standing
     * still, whatever the drive has been asked for. */
    if (p->duty_reads <= 1000) {
        return p->baseline_duty;
    }
    switch (p->mode) {
    case MOCK_DUTY_RISES:
        return p->last_openloop_rpm / 2000.0f;
    case MOCK_DUTY_DROPS:
        return (p->last_openloop_rpm < 600.0f) ? 0.3f : 0.05f;
    default:
        return p->spinup_duty;
    }
}

static edge_status_t mock_enter_config(void *self, float current_kp, float current_ki) {
    mock_flux_plant_t *p = (mock_flux_plant_t *)self;
    p->config_entered = true;
    p->enter_calls++;
    p->kp_in = current_kp;
    p->ki_in = current_ki;
    return EDGE_OK;
}

static edge_status_t mock_leave_config(void *self) {
    mock_flux_plant_t *p = (mock_flux_plant_t *)self;
    p->config_entered = false;
    p->leave_calls++;
    return EDGE_OK;
}

/* The reference ramps the speed by erpm_per_sec / 1000 per millisecond and expects the caller to
 * have applied the direction, so this records what it was given. */
static edge_status_t mock_set_openloop(void *self, float current_a, float rpm) {
    mock_flux_plant_t *p = (mock_flux_plant_t *)self;
    p->openloop_calls++;
    p->last_openloop_current = current_a;
    p->last_openloop_rpm = rpm;
    return EDGE_OK;
}

static edge_status_t mock_read_vdq(void *self, float *v_d, float *v_q) {
    mock_flux_plant_t *p = (mock_flux_plant_t *)self;
    *v_d = 0.0f;
    *v_q = p->v_q;
    return EDGE_OK;
}

static edge_status_t mock_read_idq(void *self, float *i_d, float *i_q) {
    (void)self;
    *i_d = 0.0f;
    *i_q = 0.0f;
    return EDGE_OK;
}

static edge_status_t mock_read_duty(void *self, float *duty_now) {
    mock_flux_plant_t *p = (mock_flux_plant_t *)self;
    p->duty_reads++;
    *duty_now = mock_flux_duty(p);
    return EDGE_OK;
}

static edge_status_t mock_read_speed(void *self, float *rad_s) {
    *rad_s = ((const mock_flux_plant_t *)self)->rad_s;
    return EDGE_OK;
}

static uint32_t mock_flux_fault(void *self) {
    const mock_flux_plant_t *p = (const mock_flux_plant_t *)self;
    if (p->fault_after_openloop_calls != 0u &&
        (uint32_t)p->openloop_calls >= p->fault_after_openloop_calls) {
        return 7u;
    }
    return 0u;
}

/* The stop the procedure asks for: the fixture's motor coasts from here on. */
static edge_status_t mock_flux_stop(void *self) {
    ((mock_flux_plant_t *)self)->stopped = true;
    return EDGE_OK;
}

/* The resistance measurement's four callbacks are part of the same port, and motor_id_init asks for
 * them whatever procedure is about to run; the flux cases here do not use them. */
static edge_status_t mock_flux_unused_set_phase_override(void *self, float angle_rad, bool enable) {
    (void)self;
    (void)angle_rad;
    (void)enable;
    return EDGE_OK;
}

static edge_status_t mock_flux_unused_set_current(void *self, float iq) {
    (void)self;
    (void)iq;
    return EDGE_OK;
}

static edge_status_t mock_flux_unused_reset_samples(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t mock_flux_unused_read_samples(void *self, float *i_sum, float *v_sum,
                                                   uint32_t *count) {
    (void)self;
    (void)i_sum;
    (void)v_sum;
    (void)count;
    return EDGE_OK;
}

static motor_id_measure_port_t make_flux_port(mock_flux_plant_t *plant) {
    return (motor_id_measure_port_t){
        .self = plant,
        .set_phase_override = mock_flux_unused_set_phase_override,
        .set_current = mock_flux_unused_set_current,
        .reset_samples = mock_flux_unused_reset_samples,
        .read_samples = mock_flux_unused_read_samples,
        .enter_measurement_config = mock_enter_config,
        .leave_measurement_config = mock_leave_config,
        .set_openloop_current = mock_set_openloop,
        .read_vdq = mock_read_vdq,
        .read_idq = mock_read_idq,
        .read_duty = mock_read_duty,
        .read_speed_rad_s = mock_read_speed,
        .get_fault = mock_flux_fault,
        .stop = mock_flux_stop,
    };
}

/* The procedure runs on milliseconds, so the test advances it a millisecond at a time. */
static void run_flux_ms(motor_id_app_t *app) {
    assert_int_equal(motor_id_step(app, 0.001f), EDGE_OK);
}

static void run_flux_to_end(motor_id_app_t *app) {
    for (int ms = 0;
         ms < 60000 && app->state != MOTOR_ID_STATE_COMPLETE && app->state != MOTOR_ID_STATE_FAILED;
         ms++) {
        run_flux_ms(app);
    }
}

static void test_motor_id_flux_linkage_openloop(void **state) {
    (void)state;
    /* A volt of q axis at a thousand electrical rpm: 1 / (1000 * 2*pi/60) = 9.5493e-3. */
    mock_flux_plant_t plant = {.mode = MOCK_DUTY_RISES,
                               .baseline_duty = 0.1f,
                               .spinup_duty = 0.1f,
                               .v_q = 1.0f,
                               .rad_s = 1000.0f * (float)(2.0 * 3.14159265358979323846 / 60.0)};
    motor_id_measure_port_t port = make_flux_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);

    assert_int_equal(motor_id_measure_flux_linkage_openloop(&app, 5.0f, 0.5f, 2000.0f, 0.05f, 1e-5f,
                                                            0.0f, 0.0f, 1.0f),
                     EDGE_OK);
    run_flux_to_end(&app);

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    const motor_id_result_t *result = motor_id_get_result(&app);
    assert_true(result->valid);
    /* The spin-up left the ramp at the speed where the duty reached the target: 0.5 * 2000, and
     * 1000 electrical rpm is 104.7198 rad/s. */
    assert_float_equal(app.flux_rpm_now, 1000.0f, 2.0f);
    assert_float_equal(app.flux_duty_still, 0.1f, 1e-3f);
    /* Ten thousand milliseconds of a volt on the q axis, and ten thousand samples. */
    assert_float_equal(app.flux_samples, 10000.0f, 1.0f);
    assert_float_equal(app.flux_vq_sum, 10000.0f, 1.0f);
    assert_float_equal(result->flux_linkage_wb, 9.5493e-3f, 1e-4f);
    assert_float_equal(result->linkage_undriven_wb, 9.5493e-3f, 1e-4f);
    assert_float_equal(result->undriven_samples, 2000.0f, 1.0f);
    /* The temporary configuration went in once and came back out once. */
    assert_int_equal(plant.enter_calls, 1);
    assert_int_equal(plant.leave_calls, 1);
    assert_false(plant.config_entered);
    /* The ramp holds the speed at zero, which is what makes it a standstill measurement. */
    assert_float_equal(plant.last_openloop_current, 5.0f, 1e-3f);
    assert_true(plant.last_openloop_rpm > 0.0f);
}

static void test_motor_id_flux_gives_up_when_it_must(void **state) {
    (void)state;

    /* -1: the spin-up never reaches the target within fifteen seconds. The speed ramp is slow
     * enough here that the twelve-thousand exit does not come first. */
    mock_flux_plant_t slow = {
        .mode = MOCK_DUTY_FLAT, .baseline_duty = 0.1f, .spinup_duty = 0.1f, .rad_s = 100.0f};
    motor_id_measure_port_t slow_port = make_flux_port(&slow);
    motor_id_app_t slow_app;
    motor_id_construct(&slow_app, EDGE_MOD_MOTOR_ID, 40u, &slow_port);
    assert_int_equal(motor_id_init(&slow_app), EDGE_OK);
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&slow_app, 5.0f, 0.9f, 0.1f, 0.05f,
                                                            1e-5f, 0.0f, 0.0f, 1.0f),
                     EDGE_OK);
    run_flux_to_end(&slow_app);
    assert_int_equal(slow_app.state, MOTOR_ID_STATE_FAILED);
    assert_float_equal(slow_app.flux_fail_reason, -1.0f, 1e-6f);
    assert_false(motor_id_get_result(&slow_app)->valid);
    assert_int_equal(slow.leave_calls, 1);

    /* -2: the duty falls to well under the highest it reached, after four seconds. */
    mock_flux_plant_t drop = {
        .mode = MOCK_DUTY_DROPS, .baseline_duty = 0.1f, .spinup_duty = 0.1f, .rad_s = 100.0f};
    motor_id_measure_port_t drop_port = make_flux_port(&drop);
    motor_id_app_t drop_app;
    motor_id_construct(&drop_app, EDGE_MOD_MOTOR_ID, 40u, &drop_port);
    assert_int_equal(motor_id_init(&drop_app), EDGE_OK);
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&drop_app, 5.0f, 0.9f, 2000.0f, 0.05f,
                                                            1e-5f, 0.0f, 0.0f, 1.0f),
                     EDGE_OK);
    run_flux_to_end(&drop_app);
    assert_int_equal(drop_app.state, MOTOR_ID_STATE_FAILED);
    assert_float_equal(drop_app.flux_fail_reason, -2.0f, 1e-6f);
    assert_int_equal(drop.leave_calls, 1);

    /* -3: the target is not above what the motor already drew standing still, so there is nothing
     * to spin up to. The baseline is 0.45 and the target 0.4, which is under 0.45 * 1.1. */
    mock_flux_plant_t still = {
        .mode = MOCK_DUTY_FLAT, .baseline_duty = 0.45f, .spinup_duty = 0.3f, .rad_s = 100.0f};
    motor_id_measure_port_t still_port = make_flux_port(&still);
    motor_id_app_t still_app;
    motor_id_construct(&still_app, EDGE_MOD_MOTOR_ID, 40u, &still_port);
    assert_int_equal(motor_id_init(&still_app), EDGE_OK);
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&still_app, 5.0f, 0.4f, 2000.0f, 0.05f,
                                                            1e-5f, 0.0f, 0.0f, 1.0f),
                     EDGE_OK);
    run_flux_to_end(&still_app);
    assert_int_equal(still_app.state, MOTOR_ID_STATE_FAILED);
    assert_float_equal(still_app.flux_fail_reason, -3.0f, 1e-6f);
    assert_int_equal(still.leave_calls, 1);

    /* A fault during the ramp stops it as well, and the configuration still comes back. */
    mock_flux_plant_t faulty = {.mode = MOCK_DUTY_FLAT,
                                .baseline_duty = 0.1f,
                                .spinup_duty = 0.1f,
                                .fault_after_openloop_calls = 1u,
                                .rad_s = 100.0f};
    motor_id_measure_port_t faulty_port = make_flux_port(&faulty);
    motor_id_app_t faulty_app;
    motor_id_construct(&faulty_app, EDGE_MOD_MOTOR_ID, 40u, &faulty_port);
    assert_int_equal(motor_id_init(&faulty_app), EDGE_OK);
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&faulty_app, 5.0f, 0.5f, 2000.0f, 0.05f,
                                                            1e-5f, 0.0f, 0.0f, 1.0f),
                     EDGE_OK);
    run_flux_to_end(&faulty_app);
    assert_int_equal(faulty_app.state, MOTOR_ID_STATE_FAILED);
    assert_int_equal(motor_id_get_fault(&faulty_app), 7u);
    assert_int_equal(faulty.leave_calls, 1);
}

static void test_motor_id_flux_needs_its_ports(void **state) {
    (void)state;
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, NULL);
    assert_int_equal(motor_id_init(&app), EDGE_EINVAL);
    /* A port without the flux callbacks cannot run the flux procedure, and says so. */
    mock_plant_t plant = {.r_ohm = 0.05f, .accumulate = true};
    motor_id_measure_port_t port = make_port(&plant);
    motor_id_app_t resistance_only;
    motor_id_construct(&resistance_only, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&resistance_only), EDGE_OK);
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&resistance_only, 5.0f, 0.5f, 2000.0f,
                                                            0.05f, 1e-5f, 0.0f, 0.0f, 1.0f),
                     EDGE_EINVAL);
    assert_int_equal(motor_id_measure_flux_linkage_openloop(NULL, 5.0f, 0.5f, 2000.0f, 0.05f, 1e-5f,
                                                            0.0f, 0.0f, 1.0f),
                     EDGE_EINVAL);
}

/*
 * The arms the runs above do not reach: the resistance and inductance defaults when none are
 * supplied, a duty capped at nine tenths of what the configuration allows, a second run started
 * from the state an earlier one left, and the busy refusal while one is in progress.
 */
static void test_motor_id_flux_arms(void **state) {
    (void)state;
    mock_flux_plant_t plant = {
        .mode = MOCK_DUTY_FLAT, .baseline_duty = 0.1f, .spinup_duty = 0.1f, .rad_s = 100.0f};
    motor_id_measure_port_t port = make_flux_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);

    /* The resistance and inductance come from the configuration, and 0.95 is over the 0.18 cap, so
     * the target is the cap - which this duty never reaches, so the run times out like -1. */
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&app, 5.0f, 0.95f, 1.0f, 0.0f, 0.0f,
                                                            0.05f, 1e-5f, 0.2f),
                     EDGE_OK);
    run_flux_to_end(&app);
    assert_int_equal(app.state, MOTOR_ID_STATE_FAILED);
    assert_float_equal(app.flux_fail_reason, -1.0f, 1e-6f);

    /* A second run from the failed state is allowed: only one in progress is busy. */
    plant.mode = MOCK_DUTY_RISES;
    plant.duty_reads = 0;
    plant.stopped = false;
    plant.last_openloop_rpm = 0.0f;
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&app, 5.0f, 0.5f, 2000.0f, 0.05f, 1e-5f,
                                                            0.0f, 0.0f, 1.0f),
                     EDGE_OK);
    assert_int_equal(motor_id_measure_flux_linkage_openloop(&app, 5.0f, 0.5f, 2000.0f, 0.05f, 1e-5f,
                                                            0.0f, 0.0f, 1.0f),
                     EDGE_EBUSY);
    run_flux_to_end(&app);
    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
}

/*
 * The sensored flux-linkage procedure (conf_general.c:742-899). Its plant is a motor that spins up
 * when it is driven, and the two things worth pinning are that it averages two thousand
 * milliseconds of bus voltage times duty before dividing, and that a motor which never reaches half
 * the target costs one attempt and then a release before the next one is tried.
 */
typedef struct mock_sensored_plant {
    float duty;
    float duty_target;
    float v_bus;
    float rpm;
    bool stuck; /* never reaches half the target: the attempt must give up */
    bool running;
    int enter_calls;
    int leave_calls;
    int release_calls;
    int stop_calls;
    float last_sl_min_erpm;
    float last_sl_cycle_limit;
    bool last_delay_comm_mode;
} mock_sensored_plant_t;

static edge_status_t sensored_read_duty(void *self, float *duty_now) {
    mock_sensored_plant_t *p = (mock_sensored_plant_t *)self;
    if (!p->stuck && p->duty < p->duty_target) {
        p->duty += 0.05f;
        if (p->duty > p->duty_target) {
            p->duty = p->duty_target;
        }
    }
    *duty_now = p->stuck ? 0.1f : p->duty;
    return EDGE_OK;
}

static edge_status_t sensored_read_vbus(void *self, float *v_bus) {
    *v_bus = ((mock_sensored_plant_t *)self)->v_bus;
    return EDGE_OK;
}

static edge_status_t sensored_read_rpm(void *self, float *rpm) {
    *rpm = ((mock_sensored_plant_t *)self)->rpm;
    return EDGE_OK;
}

static edge_status_t sensored_read_idq(void *self, float *i_d, float *i_q) {
    (void)self;
    *i_d = 0.0f;
    *i_q = 0.0f;
    return EDGE_OK;
}

static edge_status_t sensored_enter(void *self) {
    mock_sensored_plant_t *p = (mock_sensored_plant_t *)self;
    p->enter_calls++;
    p->running = true;
    return EDGE_OK;
}

static edge_status_t sensored_leave(void *self) {
    ((mock_sensored_plant_t *)self)->leave_calls++;
    return EDGE_OK;
}

static edge_status_t sensored_release(void *self) {
    mock_sensored_plant_t *p = (mock_sensored_plant_t *)self;
    p->release_calls++;
    p->running = false;
    return EDGE_OK;
}

static edge_status_t sensored_is_running(void *self, bool *running) {
    *running = ((mock_sensored_plant_t *)self)->running;
    return EDGE_OK;
}

static edge_status_t sensored_set_limits(void *self, float sl_min_erpm, float sl_cycle_limit,
                                         bool delay_comm_mode) {
    mock_sensored_plant_t *p = (mock_sensored_plant_t *)self;
    p->last_sl_min_erpm = sl_min_erpm;
    p->last_sl_cycle_limit = sl_cycle_limit;
    p->last_delay_comm_mode = delay_comm_mode;
    return EDGE_OK;
}

static edge_status_t sensored_set_current(void *self, float iq) {
    (void)self;
    (void)iq;
    return EDGE_OK;
}

static uint32_t sensored_no_fault(void *self) {
    (void)self;
    return 0u;
}

static edge_status_t sensored_stop(void *self) {
    mock_sensored_plant_t *p = (mock_sensored_plant_t *)self;
    p->stop_calls++;
    p->running = false;
    return EDGE_OK;
}

/* The resistance callbacks the port still has to carry, unused by this procedure. */
static edge_status_t sensored_unused_phase(void *self, float angle_rad, bool enable) {
    (void)self;
    (void)angle_rad;
    (void)enable;
    return EDGE_OK;
}

static edge_status_t sensored_unused_reset(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t sensored_unused_read(void *self, float *i_sum, float *v_sum, uint32_t *count) {
    (void)self;
    (void)i_sum;
    (void)v_sum;
    (void)count;
    return EDGE_OK;
}

static motor_id_measure_port_t make_sensored_port(mock_sensored_plant_t *plant) {
    return (motor_id_measure_port_t){
        .self = plant,
        .set_phase_override = sensored_unused_phase,
        .set_current = sensored_set_current,
        .reset_samples = sensored_unused_reset,
        .read_samples = sensored_unused_read,
        .get_fault = sensored_no_fault,
        .stop = sensored_stop,
        .leave_measurement_config = sensored_leave,
        .read_duty = sensored_read_duty,
        .read_idq = sensored_read_idq,
        .read_vbus = sensored_read_vbus,
        .read_rpm = sensored_read_rpm,
        .release_motor = sensored_release,
        .is_running = sensored_is_running,
        .set_startup_limits = sensored_set_limits,
        .enter_sensored_measurement_config = sensored_enter,
    };
}

static void run_sensored_to_end(motor_id_app_t *app) {
    for (int ms = 0;
         ms < 60000 && app->state != MOTOR_ID_STATE_COMPLETE && app->state != MOTOR_ID_STATE_FAILED;
         ms++) {
        assert_int_equal(motor_id_step(app, 0.001f), EDGE_OK);
    }
}

static void test_motor_id_flux_linkage_sensored(void **state) {
    (void)state;
    /* A bus of 24 V, a duty target of 0.5 and five hundred rpm: the average voltage is 12 V, the
     * current is zero, so the linkage is 12 / (sqrt(3) * 500 * 2*pi/60) = 0.13232. */
    mock_sensored_plant_t plant = {
        .duty = 0.0f, .duty_target = 0.5f, .v_bus = 24.0f, .rpm = 500.0f};
    motor_id_measure_port_t port = make_sensored_port(&plant);
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);

    assert_int_equal(motor_id_measure_flux_linkage_sensored(&app, 5.0f, 0.5f, 500.0f, 0.05f, 0.0f),
                     EDGE_OK);
    run_sensored_to_end(&app);

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    const motor_id_result_t *result = motor_id_get_result(&app);
    assert_true(result->valid);
    assert_float_equal(result->flux_linkage_wb, 0.13232f, 1e-4f);
    assert_int_equal(plant.enter_calls, 1);
    assert_int_equal(plant.leave_calls, 1);
    assert_int_equal(plant.release_calls, 0); /* the first attempt started it */

    /* A motor that never reaches half the target costs an attempt and a release, and the limits the
     * next attempt runs under are the reference's own. */
    mock_sensored_plant_t stuck = {
        .duty_target = 0.5f, .v_bus = 24.0f, .rpm = 500.0f, .stuck = true};
    motor_id_measure_port_t stuck_port = make_sensored_port(&stuck);
    motor_id_app_t stuck_app;
    motor_id_construct(&stuck_app, EDGE_MOD_MOTOR_ID, 40u, &stuck_port);
    assert_int_equal(motor_id_init(&stuck_app), EDGE_OK);
    assert_int_equal(
        motor_id_measure_flux_linkage_sensored(&stuck_app, 5.0f, 0.5f, 500.0f, 0.05f, 0.0f),
        EDGE_OK);
    for (int i = 0; i < 2100; i++) {
        assert_int_equal(motor_id_step(&stuck_app, 0.001f), EDGE_OK);
    }
    /* Four attempts, each after a release, and the last one has run out of tries. */
    run_sensored_to_end(&stuck_app);
    assert_int_equal(stuck_app.state, MOTOR_ID_STATE_FAILED);
    assert_false(motor_id_get_result(&stuck_app)->valid);
    assert_int_equal(stuck.release_calls, 3);
    assert_int_equal(stuck.leave_calls, 1);
    assert_float_equal(stuck.last_sl_min_erpm, 2000.0f, 1e-3f); /* 4 * min_erpm */
    assert_true(stuck.last_delay_comm_mode);

    /* Refusals: no aggregate, and a port that cannot do it. */
    assert_int_equal(motor_id_measure_flux_linkage_sensored(NULL, 5.0f, 0.5f, 500.0f, 0.05f, 0.0f),
                     EDGE_EINVAL);
    mock_plant_t plain = {.r_ohm = 0.05f, .accumulate = true};
    motor_id_measure_port_t plain_port = make_port(&plain);
    motor_id_app_t plain_app;
    motor_id_construct(&plain_app, EDGE_MOD_MOTOR_ID, 40u, &plain_port);
    assert_int_equal(motor_id_init(&plain_app), EDGE_OK);
    assert_int_equal(
        motor_id_measure_flux_linkage_sensored(&plain_app, 5.0f, 0.5f, 500.0f, 0.05f, 0.0f),
        EDGE_EINVAL);

    /*
     * A resistance of zero means the configuration's; a second run while one is in progress is
     * refused; and a run whose resistance comes from nowhere is refused rather than driven.
     */
    mock_sensored_plant_t supplied = {.duty_target = 0.5f, .v_bus = 24.0f, .rpm = 500.0f};
    motor_id_measure_port_t supplied_port = make_sensored_port(&supplied);
    motor_id_app_t supplied_app;
    motor_id_construct(&supplied_app, EDGE_MOD_MOTOR_ID, 40u, &supplied_port);
    assert_int_equal(motor_id_init(&supplied_app), EDGE_OK);
    assert_int_equal(
        motor_id_measure_flux_linkage_sensored(&supplied_app, 5.0f, 0.5f, 500.0f, 0.0f, 0.05f),
        EDGE_OK);
    assert_int_equal(
        motor_id_measure_flux_linkage_sensored(&supplied_app, 5.0f, 0.5f, 500.0f, 0.05f, 0.0f),
        EDGE_EBUSY);
    run_sensored_to_end(&supplied_app);
    assert_int_equal(supplied_app.state, MOTOR_ID_STATE_COMPLETE);
    assert_float_equal(motor_id_get_result(&supplied_app)->flux_linkage_wb, 0.13232f, 1e-4f);
    assert_int_equal(
        motor_id_measure_flux_linkage_sensored(&supplied_app, 5.0f, 0.5f, 500.0f, 0.0f, 0.0f),
        EDGE_EINVAL);
}

/* ---- Inductance, mcpwm_foc_measure_inductance (:1909-2070) ---- */

/*
 * A plant whose transform reports a known pair of axis inductances. The mean of the inverse
 * inductance is the offset, and half the split between the two axes is the amplitude the second
 * harmonic carries - and the module doubles that magnitude, so the mock reports half the split:
 *
 *   ld = 4e-5 H (25 000 1/H), lq = 6e-5 H (16 667 1/H)
 *   offset = (25 000 + 16 667) / 2 = 20 833 1/H, split = (25 000 - 16 667) / 2 = 4 167 1/H
 *
 * What comes back out is the mean of the two, and their difference, in microhenrys after the
 * reference's own 0.9 factor: 45 uH and 18 uH.
 */
typedef struct mock_ind_plant {
    float offset;
    float bin2; /* half the split, since the module doubles the magnitude it sees */
    float current_per_duty;
    float r_ohm;
    uint32_t fault;
    bool ready;
    bool bins_ok;

    int enter_calls;
    int leave_calls;
    int duty_calls;
    int stop_calls;
    int ready_calls;
    int enter_gains_calls;
    int leave_gains_calls;
    bool enter_fails; /* a configuration the plant refuses to install */
    float duty_seen;
    float current_set;
    float i_sum;
    float v_sum;
    uint32_t samples;
} mock_ind_plant_t;

static edge_status_t ind_enter_config(void *self, float duty) {
    mock_ind_plant_t *p = (mock_ind_plant_t *)self;
    p->enter_calls++;
    p->duty_seen = duty;
    return p->enter_fails ? EDGE_EINVAL : EDGE_OK;
}

static edge_status_t ind_leave_config(void *self) {
    ((mock_ind_plant_t *)self)->leave_calls++;
    return EDGE_OK;
}

static edge_status_t ind_set_duty(void *self, float duty) {
    mock_ind_plant_t *p = (mock_ind_plant_t *)self;
    p->duty_calls++;
    p->current_set = duty;
    return EDGE_OK;
}

static edge_status_t ind_is_ready(void *self, bool *ready) {
    mock_ind_plant_t *p = (mock_ind_plant_t *)self;
    p->ready_calls++;
    *ready = p->ready;
    return EDGE_OK;
}

/* The pass's reported current follows the duty the temporary configuration was entered with, which
 * is where the reference's HFI voltage - and so the current the transform measures - comes from. */
static edge_status_t ind_read_bins(void *self, float *offset, float *real2, float *imag2,
                                   float *current_mean) {
    mock_ind_plant_t *p = (mock_ind_plant_t *)self;
    if (!p->bins_ok) {
        return EDGE_EINVAL;
    }
    *offset = p->offset;
    *real2 = p->bin2;
    *imag2 = 0.0f;
    *current_mean = p->duty_seen * p->current_per_duty;
    return EDGE_OK;
}

static edge_status_t ind_set_phase_override(void *self, float angle_rad, bool enable) {
    (void)self;
    (void)angle_rad;
    (void)enable;
    return EDGE_OK;
}

static edge_status_t ind_set_current(void *self, float iq) {
    ((mock_ind_plant_t *)self)->current_set = iq;
    return EDGE_OK;
}

static edge_status_t ind_reset_samples(void *self) {
    mock_ind_plant_t *p = (mock_ind_plant_t *)self;
    p->i_sum = 0.0f;
    p->v_sum = 0.0f;
    p->samples = 0u;
    return EDGE_OK;
}

static edge_status_t ind_read_samples(void *self, float *i_sum, float *v_sum, uint32_t *count) {
    mock_ind_plant_t *p = (mock_ind_plant_t *)self;
    if (i_sum != (void *)0) {
        *i_sum = p->i_sum;
    }
    if (v_sum != (void *)0) {
        *v_sum = p->v_sum;
    }
    if (count != (void *)0) {
        *count = p->samples;
    }
    return EDGE_OK;
}

static uint32_t ind_get_fault(void *self) {
    return ((mock_ind_plant_t *)self)->fault;
}

static edge_status_t ind_stop(void *self) {
    ((mock_ind_plant_t *)self)->stop_calls++;
    return EDGE_OK;
}

static edge_status_t ind_enter_gains(void *self) {
    ((mock_ind_plant_t *)self)->enter_gains_calls++;
    return EDGE_OK;
}

static edge_status_t ind_leave_gains(void *self) {
    ((mock_ind_plant_t *)self)->leave_gains_calls++;
    return EDGE_OK;
}

/* One control cycle with whatever setpoint is in place, as the resistance measurement reads it. */
static void ind_control_cycle(mock_ind_plant_t *p) {
    p->i_sum += fabsf(p->current_set);
    p->v_sum += fabsf(p->current_set * p->r_ohm);
    p->samples++;
}

static motor_id_measure_port_t make_ind_port(mock_ind_plant_t *p) {
    return (motor_id_measure_port_t){.self = p,
                                     .set_phase_override = ind_set_phase_override,
                                     .set_current = ind_set_current,
                                     .reset_samples = ind_reset_samples,
                                     .read_samples = ind_read_samples,
                                     .get_fault = ind_get_fault,
                                     .stop = ind_stop,
                                     .enter_inductance_config = ind_enter_config,
                                     .leave_inductance_config = ind_leave_config,
                                     .set_duty = ind_set_duty,
                                     .is_hfi_ready = ind_is_ready,
                                     .read_hfi_bins = ind_read_bins,
                                     .enter_res_ind_gains = ind_enter_gains,
                                     .leave_res_ind_gains = ind_leave_gains};
}

static void make_ind_app(motor_id_app_t *app, mock_ind_plant_t *plant,
                         motor_id_measure_port_t *port) {
    *port = make_ind_port(plant);
    motor_id_construct(app, EDGE_MOD_MOTOR_ID, 40u, port);
    assert_int_equal(motor_id_init(app), EDGE_OK);
}

/*
 * The measurement over a plant whose two axis inductances are known: the temporary configuration
 * goes in, the duty is zeroed a millisecond later, the first filled buffer is waited for, and then
 * one pass per ten requested samples - each zeroing the duty, waiting its ten milliseconds and
 * reading the bins - before the current is zeroed and the configuration put back.
 */
static void test_motor_id_measure_inductance(void **state) {
    (void)state;
    mock_ind_plant_t plant = {.offset = 20833.333f,
                              .bin2 = 2083.333f,
                              .ready = true,
                              .bins_ok = true,
                              .current_per_duty = 1.0f};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_OK);
    assert_int_equal(plant.enter_calls, 1);
    assert_int_equal(plant.leave_calls, 0);
    assert_float_equal(plant.duty_seen, 0.5f, 1e-6f);

    /* One millisecond of configuration settle, then the duty is zeroed. */
    assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    assert_int_equal(app.state, MOTOR_ID_STATE_IND_DUTY_ZERO);
    assert_int_equal(plant.duty_calls, 1);

    /* A second millisecond, then the ready wait, which this plant answers at once. */
    assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    assert_int_equal(app.state, MOTOR_ID_STATE_IND_WAIT_READY);
    assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    assert_int_equal(app.state, MOTOR_ID_STATE_IND_SAMPLE);

    /* The pass's ten milliseconds: the last of them reads the bins. */
    assert_int_equal(motor_id_step(&app, 0.001f),
                     EDGE_OK); /* out of the pass's top into its wait */
    assert_int_equal(app.state, MOTOR_ID_STATE_IND_SAMPLE_WAIT);
    int waits = 0;
    while (app.state == MOTOR_ID_STATE_IND_SAMPLE_WAIT && waits < 20) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
        waits++;
    }
    assert_int_equal(waits, 10);
    assert_true(app.state == MOTOR_ID_STATE_IND_SAMPLE_READ ||
                app.state == MOTOR_ID_STATE_IND_SAMPLE || app.state == MOTOR_ID_STATE_COMPLETE);

    /* Ten samples is one pass, so the next millisecond finishes it. */
    for (int i = 0; i < 4 && app.state != MOTOR_ID_STATE_COMPLETE; ++i) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }
    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_true(app.result.valid);
    assert_int_equal(plant.leave_calls, 1);
    assert_float_equal(app.result.ind_uh, 45.0f, 1e-2f);
    assert_float_equal(app.result.ld_lq_diff_uh, 18.0f, 1e-2f);
    assert_float_equal(app.result.ind_current_a, 0.5f, 1e-4f); /* 0.5 duty * 1.0 A per duty */
}

/* The sample count is floored at ten, which is one pass, and a finer request is honest about how
 * many passes it averaged. */
static void test_motor_id_inductance_pass_count(void **state) {
    (void)state;
    mock_ind_plant_t plant = {
        .offset = 20833.333f, .bin2 = 2083.333f, .ready = true, .bins_ok = true};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    /* Under ten becomes the floor: one pass. */
    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 1u), EDGE_OK);
    assert_int_equal(app.ind_samples, 10u);
}

/* A port without the HFI callbacks is refused rather than measuring nothing. */
static void test_motor_id_inductance_needs_its_ports(void **state) {
    (void)state;
    mock_ind_plant_t plant = {
        .offset = 20833.333f, .bin2 = 2083.333f, .ready = true, .bins_ok = true};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    assert_int_equal(motor_id_measure_inductance(NULL, 0.5f, 10u), EDGE_EINVAL);

    port.read_hfi_bins = (void *)0;
    motor_id_app_t bare;
    motor_id_construct(&bare, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_measure_inductance(&bare, 0.5f, 10u), EDGE_EINVAL);

    /* And a measurement already running is busy, not restarted. */
    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_OK);
    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_EBUSY);
}

/*
 * The ready wait gives up after a hundred milliseconds and carries on, which is what the
 * reference's own break does - and the configuration still comes back afterwards.
 */
static void test_motor_id_inductance_ready_wait_gives_up(void **state) {
    (void)state;
    mock_ind_plant_t plant = {
        .offset = 20833.333f, .bin2 = 2083.333f, .ready = false, .bins_ok = true};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_OK);
    assert_int_equal(motor_id_step(&app, 0.002f), EDGE_OK); /* the two settle milliseconds */
    assert_int_equal(app.state, MOTOR_ID_STATE_IND_WAIT_READY);

    for (int i = 0; i < 102; ++i) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }
    assert_int_equal(plant.ready_calls > 100, 1);
    assert_true(app.state != MOTOR_ID_STATE_IND_WAIT_READY);
}

/* A fault during a pass stops the measurement, restores the configuration and says so. */
static void test_motor_id_inductance_aborts_on_a_fault(void **state) {
    (void)state;
    mock_ind_plant_t plant = {
        .offset = 20833.333f, .bin2 = 2083.333f, .ready = true, .bins_ok = true};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_OK);
    plant.fault = 7u;
    for (int i = 0; i < 10 && app.state != MOTOR_ID_STATE_FAILED; ++i) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }
    assert_int_equal(app.state, MOTOR_ID_STATE_FAILED);
    assert_int_equal(app.fault_code, 7u);
    assert_false(app.result.valid);
    assert_int_equal(plant.leave_calls, 1);
    assert_int_equal(plant.stop_calls, 1);
}

/*
 * The duty scan of mcpwm_foc_measure_inductance_current (:2089-2103): whole measurements at 0.02
 * and then half again times larger until the current they report reaches the caller's goal, and
 * then one more at that same duty with the caller's sample count.
 */
static void test_motor_id_inductance_current_scans_the_duty(void **state) {
    (void)state;
    /* 100 A per unit of duty: 0.02 draws 2 A, 0.03 draws 3 A and so on. */
    mock_ind_plant_t plant = {.offset = 20833.333f,
                              .bin2 = 2083.333f,
                              .ready = true,
                              .bins_ok = true,
                              .current_per_duty = 100.0f};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    assert_int_equal(motor_id_measure_inductance_current(&app, 5.0f, 30u), EDGE_OK);

    for (int i = 0; i < 2000 && app.state != MOTOR_ID_STATE_COMPLETE; ++i) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }
    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);

    /* The walk stopped at the first duty whose current reached the goal: 0.02, 0.03, 0.045,
     * 0.0675 - the last of those draws 6.75 A, over the 5 A asked for - and the final measurement
     * repeated that duty with the caller's thirty samples, which is three passes. */
    assert_float_equal(plant.duty_seen, 0.0675f, 1e-6f);
    assert_float_equal(app.result.ind_current_a, 6.75f, 1e-3f);
    assert_float_equal(app.result.ind_uh, 45.0f, 1e-2f);
    assert_int_equal(plant.enter_calls, 5); /* four in the walk, one final */
}

/*
 * The composed sequence (:2320-2360): resistance at 2 A and half again times larger until the
 * current exceeds 1/R - with the current loop's gains turned down for it and put back on the way
 * out - then a 200-sample resistance, ten milliseconds at zero current, and inductance at the same
 * current.
 */
static void test_motor_id_measure_r_l_runs_the_whole_sequence(void **state) {
    (void)state;
    mock_ind_plant_t plant = {
        .offset = 20833.333f, .bin2 = 2083.333f, .ready = true, .bins_ok = true, .r_ohm = 0.05f};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    assert_int_equal(motor_id_measure_r_l(&app, 50.0f), EDGE_OK);
    assert_int_equal(plant.enter_gains_calls, 1);
    assert_int_equal(plant.leave_gains_calls, 0); /* still held while it runs */

    for (int i = 0;
         i < 20000 && app.state != MOTOR_ID_STATE_COMPLETE && app.state != MOTOR_ID_STATE_FAILED;
         ++i) {
        ind_control_cycle(&plant);
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_true(app.result.valid);
    assert_float_equal(app.result.r_ohm, 0.05f, 1e-4f);
    assert_float_equal(app.result.ind_uh, 45.0f, 1e-2f);
    assert_int_equal(plant.enter_gains_calls, 1);
    assert_int_equal(plant.leave_gains_calls, 1); /* put back exactly once, at the single exit */
    /* The scan stops at the first current past 1/R = 20 A, which the walk reaches at 22.78 A. */
    assert_float_equal(app.res_ind_last_current_a, 22.78125f, 1e-3f);
}

/*
 * The ways out of the sequence that are not the happy one: a transform that cannot answer, a
 * configuration that cannot be installed, a current limit with no room for the scan, a zero
 * resistance, and a second procedure asked for while one is running.
 */
static void test_motor_id_inductance_failure_paths(void **state) {
    (void)state;

    /* The transform refuses to answer: the pass fails and the configuration comes back. */
    mock_ind_plant_t blind = {.offset = 20833.333f, .bin2 = 2083.333f, .ready = true};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &blind, &port);
    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_OK);
    for (int i = 0; i < 200 && app.state != MOTOR_ID_STATE_FAILED; ++i) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }
    assert_int_equal(app.state, MOTOR_ID_STATE_FAILED);
    assert_false(app.result.valid);
    assert_int_equal(blind.leave_calls, 1);
    assert_int_equal(blind.stop_calls, 1);

    /* The configuration itself is refused, so nothing is started. */
    mock_ind_plant_t strict = {.offset = 20833.333f,
                               .bin2 = 2083.333f,
                               .ready = true,
                               .bins_ok = true,
                               .enter_fails = true};
    make_ind_app(&app, &strict, &port);
    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_EINVAL);
    assert_int_equal(app.state, MOTOR_ID_STATE_FAILED);

    /* One procedure at a time. */
    mock_ind_plant_t plant = {.offset = 20833.333f,
                              .bin2 = 2083.333f,
                              .ready = true,
                              .bins_ok = true,
                              .current_per_duty = 100.0f};
    make_ind_app(&app, &plant, &port);
    assert_int_equal(motor_id_measure_inductance_current(&app, 5.0f, 10u), EDGE_OK);
    assert_int_equal(motor_id_measure_inductance_current(&app, 5.0f, 10u), EDGE_EBUSY);
    assert_int_equal(motor_id_measure_inductance(&app, 0.5f, 10u), EDGE_EBUSY);
    assert_int_equal(motor_id_measure_r_l(&app, 50.0f), EDGE_EBUSY);
}

/*
 * The composed sequence's other two exits (:2341-2343 and :2331-2334): a current limit with no room
 * for the walk takes half of it straight away, and a resistance that comes back zero ends there -
 * with the current loop's gains put back on both paths.
 */
static void test_motor_id_measure_r_l_edge_exits(void **state) {
    (void)state;
    mock_ind_plant_t plant;
    motor_id_measure_port_t port;
    motor_id_app_t app;

    /* current_max / 2 is 2 A, where the walk starts, so it never runs and the limit's half is the
     * current the final measurement is taken at. */
    memset(&plant, 0, sizeof(plant));
    plant.offset = 20833.333f;
    plant.bin2 = 2083.333f;
    plant.ready = true;
    plant.bins_ok = true;
    plant.r_ohm = 0.05f;
    make_ind_app(&app, &plant, &port);
    assert_int_equal(motor_id_measure_r_l(&app, 4.0f), EDGE_OK);
    for (int i = 0; i < 20000 && app.state != MOTOR_ID_STATE_COMPLETE; ++i) {
        ind_control_cycle(&plant);
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }
    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_float_equal(app.res_ind_last_current_a, 2.0f, 1e-6f);
    assert_float_equal(app.result.ind_uh, 45.0f, 1e-2f);
    assert_int_equal(plant.leave_gains_calls, 1);

    /* A resistance of zero ends the sequence at its own exit label, with the gains back. */
    memset(&plant, 0, sizeof(plant));
    plant.offset = 20833.333f;
    plant.bin2 = 2083.333f;
    plant.ready = true;
    plant.bins_ok = true;
    make_ind_app(&app, &plant, &port);
    assert_int_equal(motor_id_measure_r_l(&app, 50.0f), EDGE_OK);
    for (int i = 0; i < 20000 && app.state != MOTOR_ID_STATE_COMPLETE; ++i) {
        ind_control_cycle(&plant);
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }
    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_float_equal(app.result.r_ohm, 0.0f, 1e-9f);
    /* The resistance result stands as whatever it measured - the reference publishes it before its
     * exit - while the inductance half of the sequence never ran at all. */
    assert_float_equal(app.result.ind_uh, 0.0f, 1e-9f);
    assert_int_equal(plant.leave_gains_calls, 1);
}

/*
 * The gains a detection exists to produce, against the reference's own output: conf_general.c's
 * conf_general_calc_apply_foc_cc_kp_ki_gain was compiled verbatim - a flat struct in place of
 * mc_configuration - and run on these three inputs. The observer's gain is compared in millions
 * because it is a number in the tens of millions in volts per weber, where the last float digit is
 * noise; the third case is what the crossover argument is for, half the bandwidth and kp and ki
 * both doubled with it.
 */
static void test_motor_id_gains_match_the_reference(void **state) {
    (void)state;

    motor_id_gains_t g = motor_id_calc_apply_foc_gains(0.045f, 0.000045f, 0.004f, 1000.0f);
    assert_float_equal(g.current_kp, 0.0450000018f, 1e-7f);
    assert_float_equal(g.current_ki, 45.0f, 1e-5f);
    assert_float_equal(g.observer_gain / 1e6f, 62.499992f, 1e-4f);

    g = motor_id_calc_apply_foc_gains(0.0125f, 0.000018f, 0.00123f, 1000.0f);
    assert_float_equal(g.current_kp, 0.0180000011f, 1e-7f);
    assert_float_equal(g.current_ki, 12.5f, 1e-5f);
    assert_float_equal(g.observer_gain / 1e6f, 660.982208f, 1e-3f);

    g = motor_id_calc_apply_foc_gains(0.1f, 0.0002f, 0.02f, 500.0f);
    assert_float_equal(g.current_kp, 0.399999976f, 1e-6f);
    assert_float_equal(g.current_ki, 200.0f, 1e-4f);
    assert_float_equal(g.observer_gain / 1e6f, 2.5f, 1e-5f);
}

/*
 * The all-in-one detection's own measurement, conf_general.c:1528's measure_r_l_imax, driven to its
 * end: the probe walk, the resistance at the current it settled on, the two inductances there, and
 * the ceiling the reference derives - sqrt(max_power_loss / r / 1.5) truncated by the board's
 * limit.
 *
 * The walk's numbers are deterministic and worth reading out of the test: it starts at a fiftieth
 * of the ceiling (1 A) and grows by half again, and it stops on the first probe whose dissipated
 * power i * i * r * 1.5 reaches a fifth of the loss allowed - which is 1.5^6, 11.390625 A. The
 * ceiling it derives, sqrt(30 / 0.05 / 1.5), is 20 A, under the board's 250 and so not truncated by
 * it.
 */
static void test_motor_id_measure_r_l_imax_runs_to_its_ceiling(void **state) {
    (void)state;
    mock_ind_plant_t plant = {
        .offset = 20833.333f, .bin2 = 2083.333f, .ready = true, .bins_ok = true, .r_ohm = 0.05f};
    motor_id_measure_port_t port;
    motor_id_app_t app;
    make_ind_app(&app, &plant, &port);

    assert_int_equal(motor_id_measure_r_l_imax(&app, 50.0f, 0.05f, 30.0f, 250.0f), EDGE_OK);
    for (int i = 0;
         i < 40000 && app.state != MOTOR_ID_STATE_COMPLETE && app.state != MOTOR_ID_STATE_FAILED;
         ++i) {
        ind_control_cycle(&plant);
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_true(app.result.valid);
    assert_float_equal(app.result.r_ohm, 0.05f, 1e-4f);
    assert_float_equal(app.result.ind_uh, 45.0f, 1e-2f);
    assert_float_equal(app.imax_last_a, 11.390625f, 1e-3f);
    assert_float_equal(app.result.i_max_a, 20.0f, 1e-3f);
}

/*
 * The hall-detect procedure's own three pieces (mcpwm_foc.c:2440-2474 and util/utils_sys.c:92-115),
 * each against the harness run of the reference's own code.
 *
 * The table first: six readings seen forty times each name the angles their sums average to -
 * nought, sixty, a hundred and twenty, and so on, each scaled to the two hundred counts an entry
 * holds. A reading that comes out negative is normalized into the same range, which is what makes
 * two hundred and twenty-five degrees a hundred and twenty-five. The two readings short of samples
 * name nothing, and the verdict is the reference's own: exactly two of them pass.
 */
static void test_motor_id_hall_angle_table_matches_the_reference(void **state) {
    (void)state;

    float sin_hall[8] = {0};
    float cos_hall[8] = {0};
    int iterations[8] = {0};
    uint8_t table[8];
    bool result = false;

    for (int i = 1; i < 7; i++) {
        const float ang = (float)((i - 1) * 60) * (float)M_PI / 180.0f;
        sin_hall[i] = sinf(ang) * 40.0f;
        cos_hall[i] = cosf(ang) * 40.0f;
        iterations[i] = 40;
    }
    iterations[0] = 10;
    iterations[7] = 3;

    assert_int_equal(motor_id_hall_angle_table(sin_hall, cos_hall, iterations, table, &result), 2);
    assert_true(result);
    const uint8_t expected[8] = {255u, 0u, 33u, 66u, 100u, 133u, 166u, 255u};
    assert_memory_equal(table, expected, sizeof(expected));

    /* A reading whose angle comes out negative is normalized rather than wrapped by a modulo. */
    iterations[0] = 40;
    sin_hall[0] = -1.0f;
    cos_hall[0] = -1.0f;
    assert_int_equal(motor_id_hall_angle_table(sin_hall, cos_hall, iterations, table, &result), 1);
    assert_false(result);
    assert_int_equal(table[0], 125u);

    /* One short reading is not the pass mark either. */
    iterations[7] = 40;
    iterations[6] = 4;
    assert_int_equal(motor_id_hall_angle_table(sin_hall, cos_hall, iterations, table, &result), 1);
    assert_false(result);
    assert_int_equal(table[6], 255u);

    /* Guards: nothing to read, and nothing to write into. */
    assert_int_equal(motor_id_hall_angle_table(NULL, cos_hall, iterations, table, &result), 0);
    assert_int_equal(motor_id_hall_angle_table(sin_hall, NULL, iterations, table, &result), 0);
    assert_int_equal(motor_id_hall_angle_table(sin_hall, cos_hall, NULL, table, &result), 0);
    assert_int_equal(motor_id_hall_angle_table(sin_hall, cos_hall, iterations, NULL, &result), 0);
    assert_int_equal(motor_id_hall_angle_table(sin_hall, cos_hall, iterations, table, NULL), 0);
}

/*
 * A hall reading's majority, from the harness run of utils_read_hall_hw: the first six are that
 * run's one read per pin when no extra samples are asked for and every pin's own bit, and the rest
 * are the same two lines' majority cases. The comparison is strict, so a pin that reaches exactly
 * half the count is not a majority and stays low, which is the reference's own `h1 > tres`.
 */
static void test_motor_id_hall_majority_matches_the_reference(void **state) {
    (void)state;

    assert_int_equal(motor_id_hall_majority(0, 0, 0, 1), 0);
    assert_int_equal(motor_id_hall_majority(1, 0, 0, 1), 1);
    assert_int_equal(motor_id_hall_majority(0, 1, 0, 1), 2);
    assert_int_equal(motor_id_hall_majority(0, 0, 1, 1), 4);
    assert_int_equal(motor_id_hall_majority(1, 1, 0, 1), 3);
    assert_int_equal(motor_id_hall_majority(1, 1, 1, 1), 7);

    /* One high read out of three, five or seven is not a majority. */
    assert_int_equal(motor_id_hall_majority(1, 0, 0, 3), 0);
    assert_int_equal(motor_id_hall_majority(1, 1, 0, 5), 0);
    assert_int_equal(motor_id_hall_majority(1, 1, 1, 7), 0);

    /* The majority itself, which needs more than half. */
    assert_int_equal(motor_id_hall_majority(2, 0, 0, 3), 1);
    assert_int_equal(motor_id_hall_majority(3, 3, 0, 5), 3);
    assert_int_equal(motor_id_hall_majority(4, 4, 4, 7), 7);
}

/*
 * The sweep's accumulation, from the same run: the angle joins the reading's own sums and its
 * count, and the readings the sweep did not visit are left as they were.
 */
static void test_motor_id_hall_accumulate_matches_the_reference(void **state) {
    (void)state;

    float sin_hall[8] = {0};
    float cos_hall[8] = {0};
    int iterations[8] = {0};

    motor_id_hall_accumulate(sin_hall, cos_hall, iterations, 5u, 0.0f, 1.0f);
    motor_id_hall_accumulate(sin_hall, cos_hall, iterations, 5u, 0.0f, 1.0f);
    motor_id_hall_accumulate(sin_hall, cos_hall, iterations, 5u, 1.0f, 0.0f);

    assert_float_equal(sin_hall[5], 1.0f, 1e-6f);
    assert_float_equal(cos_hall[5], 2.0f, 1e-6f);
    assert_int_equal(iterations[5], 3);
    assert_float_equal(sin_hall[0], 0.0f, 1e-9f);
    assert_int_equal(iterations[0], 0);

    /* Guards: nothing to write into, and a reading the table does not hold. */
    motor_id_hall_accumulate(NULL, cos_hall, iterations, 1u, 1.0f, 1.0f);
    motor_id_hall_accumulate(sin_hall, NULL, iterations, 1u, 1.0f, 1.0f);
    motor_id_hall_accumulate(sin_hall, cos_hall, NULL, 1u, 1.0f, 1.0f);
    motor_id_hall_accumulate(sin_hall, cos_hall, iterations, 9u, 1.0f, 1.0f);
}

/*
 * The hall detection end to end. The mock answers as a hall sensor would: the procedure holds the
 * electrical angle with its override, and the reading is whichever of the six sectors that angle is
 * in - which is what a real sensor on a six-step motor reports. Twenty-three thousand milliseconds
 * of it later the table names each reading's own angle, and the two ends, which no sector covers,
 * name nothing.
 */
typedef struct mock_hall_ctx {
    float override_rad;
    bool override_on;
    int override_calls;
    int stop_calls;
    uint32_t fault;
} mock_hall_ctx_t;

static edge_status_t mock_hall_set_current(void *self, float iq) {
    (void)self;
    (void)iq;
    return EDGE_OK;
}

static edge_status_t mock_hall_override(void *self, float angle_rad, bool enable) {
    mock_hall_ctx_t *ctx = (mock_hall_ctx_t *)self;
    ctx->override_rad = angle_rad;
    ctx->override_on = enable;
    ctx->override_calls++;
    return EDGE_OK;
}

static uint8_t mock_hall_read(void *self) {
    const mock_hall_ctx_t *ctx = (const mock_hall_ctx_t *)self;
    /* Degrees, then the sector: nought to fifty-nine is reading one, and so on round. */
    float deg = ctx->override_rad * (float)(180.0 / 3.14159265358979323846);
    while (deg < 0.0f) {
        deg += 360.0f;
    }
    while (deg >= 360.0f) {
        deg -= 360.0f;
    }
    return (uint8_t)((int)(deg / 60.0f) + 1);
}

static uint32_t mock_hall_fault(void *self) {
    return ((const mock_hall_ctx_t *)self)->fault;
}

static edge_status_t mock_hall_stop(void *self) {
    ((mock_hall_ctx_t *)self)->stop_calls++;
    return EDGE_OK;
}

/*
 * The two callbacks the port has to carry for a measurement app to initialize at all, which the
 * hall detection never reaches: its own procedure reads the halls and nothing else.
 */
static edge_status_t mock_hall_reset_samples(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t mock_hall_read_samples(void *self, float *i_sum, float *v_sum,
                                            uint32_t *count) {
    (void)self;
    *i_sum = 0.0f;
    *v_sum = 0.0f;
    *count = 1u;
    return EDGE_OK;
}

static void test_motor_id_detect_hall_runs_the_whole_sweep(void **state) {
    (void)state;

    mock_hall_ctx_t ctx = {0};
    motor_id_measure_port_t port = {.self = &ctx,
                                    .set_current = mock_hall_set_current,
                                    .set_phase_override = mock_hall_override,
                                    .read_hall = mock_hall_read,
                                    .reset_samples = mock_hall_reset_samples,
                                    .read_samples = mock_hall_read_samples,
                                    .get_fault = mock_hall_fault,
                                    .stop = mock_hall_stop};
    motor_id_app_t app;
    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);

    assert_int_equal(motor_id_detect_hall(&app, 5.0f, 0), EDGE_OK);
    assert_true(ctx.override_on);

    for (int i = 0;
         i < 30000 && app.state != MOTOR_ID_STATE_COMPLETE && app.state != MOTOR_ID_STATE_FAILED;
         ++i) {
        assert_int_equal(motor_id_step(&app, 0.001f), EDGE_OK);
    }

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    const motor_id_result_t *result = motor_id_get_result(&app);
    assert_true(result->hall_valid);
    assert_true(result->valid);
    assert_int_equal(ctx.override_calls > 1, true);
    assert_int_equal(ctx.stop_calls, 1);
    assert_false(ctx.override_on); /* let go on the way out */

    /* Each reading's angle is its own sector's middle, and the two ends are named nothing. */
    for (int reading = 1; reading < 7; reading++) {
        const int centre = (reading - 1) * 33 + 16; /* the sector's middle in the table's counts */
        assert_true(result->hall_table[reading] > (uint8_t)(centre - 4));
        assert_true(result->hall_table[reading] < (uint8_t)(centre + 4));
    }
    assert_int_equal(result->hall_table[0], 255u);
    assert_int_equal(result->hall_table[7], 255u);

    /* Guards: a current that is not one, and a second run while one is in flight. */
    assert_int_equal(motor_id_detect_hall(&app, 0.0f, 0), EDGE_EINVAL);
    assert_int_equal(motor_id_detect_hall(&app, 5.0f, -1), EDGE_EINVAL);
    assert_int_equal(motor_id_detect_hall(NULL, 5.0f, 0), EDGE_EINVAL);
    (void)motor_id_detect_hall(&app, 5.0f, 1);
    assert_int_equal(motor_id_detect_hall(&app, 5.0f, 0), EDGE_EBUSY);

    /* A port without the halls the procedure reads cannot run it at all. */
    motor_id_measure_port_t blind = {.self = &ctx,
                                     .set_current = mock_hall_set_current,
                                     .set_phase_override = mock_hall_override,
                                     .reset_samples = mock_hall_reset_samples,
                                     .read_samples = mock_hall_read_samples,
                                     .get_fault = mock_hall_fault,
                                     .stop = mock_hall_stop};
    motor_id_app_t bare;
    motor_id_construct(&bare, EDGE_MOD_MOTOR_ID, 40u, &blind);
    assert_int_equal(motor_id_init(&bare), EDGE_OK);
    assert_int_equal(motor_id_detect_hall(&bare, 5.0f, 0), EDGE_ENOTSUP);
}

/*
 * A plant for the parameter detection (conf_general.c:514-715). Where the procedure's other plants
 * hold a motor still, this one is a drive that is spinning up: the duty it has reached and where it
 * is headed, the count its tachometer is on, and the integrator whose reading hands back the
 * average since the last one and clears itself.
 *
 * What the reference's own spin-up does through the motor is modelled by the duty walking to its
 * target a step at a time: a current asks for the spin-up duty, letting the current go lets it
 * fall, and set_duty names the target it is held at.
 */
typedef struct mock_param_plant {
    float duty;
    float duty_target;
    float duty_step;
    uint32_t tacho;
    uint32_t tacho_step;
    uint32_t fault;
    bool running;
    /* A drive that has been asked for a current or a duty is spinning, and keeps spinning - and
     * keeps counting - after the current is let go of, which is how the reference counts the
     * commutations of a motor that is coasting. */
    bool spun_up;
    float rpm;
    float v_bus;

    /* The integrator's readings in order. The reference takes four: it throws the first and the
     * third away, keeps the second as the ceiling and the fourth as the running average. */
    float integrator[4];
    int integrator_reads;

    float hall_table[8];
    int hall_res;

    /* What the plant was told. */
    float staged_min_erpm[4];
    float staged_limit[4];
    bool staged_delay[4];
    int stage_calls;
    int switch_calls;
    int disable_timeout_calls;
    int restore_timeout_calls;
    int release_calls;
    int reset_hall_calls;
    float iq_set;
} mock_param_plant_t;

static void mock_param_advance(mock_param_plant_t *p) {
    if (p->duty < p->duty_target) {
        p->duty = fminf(p->duty + p->duty_step, p->duty_target);
    } else if (p->duty > p->duty_target) {
        p->duty = fmaxf(p->duty - p->duty_step, p->duty_target);
    }
    if (p->spun_up) {
        p->tacho += p->tacho_step;
    }
}

static edge_status_t mock_param_set_current(void *self, float iq) {
    mock_param_plant_t *p = (mock_param_plant_t *)self;
    p->iq_set = iq;
    p->duty_target = (iq > 0.0f) ? 0.5f : 0.0f;
    if (iq > 0.0f) {
        p->spun_up = true;
    }
    return EDGE_OK;
}

static edge_status_t mock_param_set_duty(void *self, float duty) {
    mock_param_plant_t *p = (mock_param_plant_t *)self;
    p->duty_target = duty;
    p->spun_up = true;
    return EDGE_OK;
}

static edge_status_t mock_param_read_duty(void *self, float *duty_now) {
    *duty_now = ((mock_param_plant_t *)self)->duty;
    return EDGE_OK;
}

static uint32_t mock_param_read_tacho(void *self) {
    return ((mock_param_plant_t *)self)->tacho;
}

static float mock_param_read_integrator(void *self) {
    mock_param_plant_t *p = (mock_param_plant_t *)self;
    const float value = p->integrator[p->integrator_reads <= 3 ? p->integrator_reads : 3];
    p->integrator_reads++;
    return value;
}

static edge_status_t mock_param_stage(void *self, float min_erpm, float cycle_int_limit,
                                      bool delay_comm_mode) {
    mock_param_plant_t *p = (mock_param_plant_t *)self;
    const int i = p->stage_calls <= 3 ? p->stage_calls : 3;
    p->staged_min_erpm[i] = min_erpm;
    p->staged_limit[i] = cycle_int_limit;
    p->staged_delay[i] = delay_comm_mode;
    p->stage_calls++;
    return EDGE_OK;
}

static edge_status_t mock_param_switch_comm_mode(void *self) {
    ((mock_param_plant_t *)self)->switch_calls++;
    return EDGE_OK;
}

static edge_status_t mock_param_disable_timeout(void *self) {
    ((mock_param_plant_t *)self)->disable_timeout_calls++;
    return EDGE_OK;
}

static edge_status_t mock_param_restore_timeout(void *self) {
    ((mock_param_plant_t *)self)->restore_timeout_calls++;
    return EDGE_OK;
}

static edge_status_t mock_param_restore_config(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t mock_param_release_motor(void *self) {
    mock_param_plant_t *p = (mock_param_plant_t *)self;
    p->running = false;
    p->spun_up = false;
    p->duty_target = 0.0f;
    p->release_calls++;
    return EDGE_OK;
}

static edge_status_t mock_param_is_running(void *self, bool *running) {
    *running = ((mock_param_plant_t *)self)->running;
    return EDGE_OK;
}

static edge_status_t mock_param_reset_hall_detect(void *self) {
    ((mock_param_plant_t *)self)->reset_hall_calls++;
    return EDGE_OK;
}

static edge_status_t mock_param_read_hall_detect_result(void *self, uint8_t table[8], int *res) {
    mock_param_plant_t *p = (mock_param_plant_t *)self;
    for (int i = 0; i < 8; i++) {
        table[i] = (uint8_t)p->hall_table[i];
    }
    *res = p->hall_res;
    return EDGE_OK;
}

static edge_status_t mock_param_read_rpm(void *self, float *rpm) {
    *rpm = ((mock_param_plant_t *)self)->rpm;
    return EDGE_OK;
}

static edge_status_t mock_param_read_vbus(void *self, float *v_bus) {
    *v_bus = ((mock_param_plant_t *)self)->v_bus;
    return EDGE_OK;
}

static edge_status_t mock_param_set_phase_override(void *self, float angle_rad, bool enable) {
    (void)self;
    (void)angle_rad;
    (void)enable;
    return EDGE_OK;
}

static edge_status_t mock_param_reset_samples(void *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t mock_param_read_samples(void *self, float *i_sum, float *v_sum,
                                             uint32_t *count) {
    (void)self;
    if (i_sum != NULL) {
        *i_sum = 0.0f;
    }
    if (v_sum != NULL) {
        *v_sum = 0.0f;
    }
    if (count != NULL) {
        *count = 0u;
    }
    return EDGE_OK;
}

static edge_status_t mock_param_stop(void *self) {
    (void)self;
    return EDGE_OK;
}

static uint32_t mock_param_read_fault(void *self) {
    return ((mock_param_plant_t *)self)->fault;
}

static motor_id_measure_port_t make_param_port(mock_param_plant_t *p) {
    motor_id_measure_port_t port = {.self = p,
                                    .set_phase_override = mock_param_set_phase_override,
                                    .set_current = mock_param_set_current,
                                    .reset_samples = mock_param_reset_samples,
                                    .read_samples = mock_param_read_samples,
                                    .get_fault = mock_param_read_fault,
                                    .stop = mock_param_stop,
                                    .set_duty = mock_param_set_duty,
                                    .read_duty = mock_param_read_duty,
                                    .read_rpm = mock_param_read_rpm,
                                    .read_vbus = mock_param_read_vbus,
                                    .release_motor = mock_param_release_motor,
                                    .is_running = mock_param_is_running,
                                    .stage_bldc_config = mock_param_stage,
                                    .restore_bldc_config = mock_param_restore_config,
                                    .switch_comm_mode_delay = mock_param_switch_comm_mode,
                                    .disable_timeout = mock_param_disable_timeout,
                                    .restore_timeout = mock_param_restore_timeout,
                                    .read_tacho = mock_param_read_tacho,
                                    .read_reset_avg_cycle_integrator = mock_param_read_integrator,
                                    .reset_hall_detect = mock_param_reset_hall_detect,
                                    .read_hall_detect_result = mock_param_read_hall_detect_result};
    return port;
}

static void run_param_ms(motor_id_app_t *app, mock_param_plant_t *p, int ms) {
    for (int i = 0; i < ms; i++) {
        mock_param_advance(p);
        assert_int_equal(motor_id_step(app, 0.001f), EDGE_OK);
    }
}

/*
 * conf_general.c:705-715 and the arithmetic around it: the settings an attempt runs with, the test
 * a watched count is held to, the coupling factor and the five-step criterion.
 */
static void test_motor_id_spinup_params_match_the_reference(void **state) {
    (void)state;

    motor_id_spinup_params_t params;

    motor_id_spinup_params(0u, 200.0f, &params);
    assert_float_equal(params.sl_min_erpm, 200.0f, 1e-6f);
    assert_float_equal(params.sl_cycle_int_limit, 50.0f, 1e-6f);
    assert_false(params.delay_comm_mode);

    motor_id_spinup_params(1u, 200.0f, &params);
    assert_float_equal(params.sl_min_erpm, 400.0f, 1e-6f);
    assert_float_equal(params.sl_cycle_int_limit, 20.0f, 1e-6f);
    assert_false(params.delay_comm_mode);

    motor_id_spinup_params(2u, 200.0f, &params);
    assert_float_equal(params.sl_min_erpm, 800.0f, 1e-6f);
    assert_float_equal(params.sl_cycle_int_limit, 20.0f, 1e-6f);
    assert_true(params.delay_comm_mode);

    /* Past the third is the third, which is where the reference's own loop stops. */
    motor_id_spinup_params(9u, 200.0f, &params);
    assert_float_equal(params.sl_min_erpm, 800.0f, 1e-6f);
    assert_true(params.delay_comm_mode);

    /* Nothing to write into. */
    motor_id_spinup_params(0u, 200.0f, NULL);
}

static void test_motor_id_tacho_advanced_matches_the_reference(void **state) {
    (void)state;

    assert_false(motor_id_tacho_advanced(100u, 102u, 3u));
    assert_true(motor_id_tacho_advanced(100u, 103u, 3u));
    assert_true(motor_id_tacho_advanced(100u, 150u, 50u));
    assert_false(motor_id_tacho_advanced(100u, 149u, 50u));
    assert_true(motor_id_tacho_advanced(100u, 200u, 100u));
    /* The reference subtracts unsigned counts, so a count that has wrapped around is a count that
     * has advanced. */
    assert_true(motor_id_tacho_advanced(0xFFFFFFFEu, 1u, 3u));
}

static void test_motor_id_bemf_coupling_k_matches_the_reference(void **state) {
    (void)state;

    /* :705-708: subtract, divide, multiply - in that order and with no guard on the divisor. */
    assert_float_equal(motor_id_bemf_coupling_k(120.0f, 100.0f, 24.0f, 1000.0f),
                       (120.0f - 100.0f) / 24.0f * 1000.0f, 1e-3f);
    assert_float_equal(motor_id_bemf_coupling_k(100.0f, 100.0f, 24.0f, 1000.0f), 0.0f, 1e-6f);
    assert_true(isinf(motor_id_bemf_coupling_k(120.0f, 100.0f, 0.0f, 1000.0f)));
}

static void test_motor_id_spinup_passed_matches_the_reference(void **state) {
    (void)state;

    assert_false(motor_id_spinup_passed(0u));
    assert_false(motor_id_spinup_passed(4u));
    assert_true(motor_id_spinup_passed(5u));
    assert_false(motor_id_spinup_passed(6u));
}

/* The guards: the arguments, the state, and the callbacks the procedure cannot run without. */
static void test_motor_id_detect_motor_param_guards(void **state) {
    (void)state;
    mock_param_plant_t plant = {.v_bus = 24.0f};
    motor_id_measure_port_t port = make_param_port(&plant);
    motor_id_app_t app;

    assert_int_equal(motor_id_detect_motor_param(NULL, 5.0f, 200.0f, 0.2f), EDGE_EINVAL);

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_detect_motor_param(&app, 0.0f, 200.0f, 0.2f), EDGE_EINVAL);
    assert_int_equal(motor_id_detect_motor_param(&app, 5.0f, 0.0f, 0.2f), EDGE_EINVAL);

    /* One callback the procedure needs gone: it says it cannot run rather than running blind. */
    motor_id_measure_port_t partial = port;
    partial.read_tacho = NULL;
    motor_id_app_t partial_app;
    motor_id_construct(&partial_app, EDGE_MOD_MOTOR_ID, 40u, &partial);
    assert_int_equal(motor_id_init(&partial_app), EDGE_OK);
    assert_int_equal(motor_id_detect_motor_param(&partial_app, 5.0f, 200.0f, 0.2f), EDGE_ENOTSUP);

    /* Started, then asked again while it is running. */
    assert_int_equal(motor_id_detect_motor_param(&app, 5.0f, 200.0f, 0.2f), EDGE_OK);
    assert_int_equal(motor_id_detect_motor_param(&app, 5.0f, 200.0f, 0.2f), EDGE_EBUSY);
}

/* The run test plants share: a drive that spins up to the spin-up duty, samples its halls, and
 * coasts down to the low duty the caller asks for. */
static void mock_param_plant_init(mock_param_plant_t *p, float duty_step, uint32_t tacho_step) {
    memset(p, 0, sizeof(*p));
    p->duty_step = duty_step;
    p->tacho_step = tacho_step;
    p->v_bus = 24.0f;
    p->rpm = 1000.0f;
    p->integrator[0] = 10.0f;
    p->integrator[1] = 100.0f;
    p->integrator[2] = 0.0f;
    p->integrator[3] = 120.0f;
    for (int i = 0; i < 8; i++) {
        p->hall_table[i] = (float)(7 - i);
    }
    p->hall_res = 0;
}

/*
 * The mid-watch switch (conf_general.c:594-613): the duty passes half of the spin-up value, the
 * reference switches the commutation mode there, and the switch is what tells it the motor is
 * running - so the attempt loop stops after the one that switched.
 */
static void test_motor_id_detect_motor_param_switches_commutation(void **state) {
    (void)state;
    mock_param_plant_t plant;
    mock_param_plant_init(&plant, 0.01f, 2u);
    motor_id_measure_port_t port = make_param_port(&plant);
    motor_id_app_t app;

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_detect_motor_param(&app, 5.0f, 200.0f, 0.2f), EDGE_OK);

    run_param_ms(&app, &plant, 4000);

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_int_equal(plant.switch_calls, 1);
    /* Only the staging and no re-staging: the switch ended the attempts. */
    assert_int_equal(plant.stage_calls, 1);
    assert_float_equal(plant.staged_min_erpm[0], 200.0f, 1e-6f);
    assert_float_equal(plant.staged_limit[0], 50.0f, 1e-6f);
    assert_false(plant.staged_delay[0]);
    assert_int_equal(plant.disable_timeout_calls, 1);
    assert_int_equal(plant.restore_timeout_calls, 1);
    assert_int_equal(plant.reset_hall_calls, 1);
    assert_int_equal(plant.release_calls, 1);
    assert_float_equal(plant.iq_set, 0.0f, 1e-6f);

    const motor_id_result_t *result = motor_id_get_result(&app);
    assert_true(result->valid);
    assert_float_equal(result->int_limit, 100.0f, 1e-6f);
    assert_float_equal(result->bemf_coupling_k, (120.0f - 100.0f) / 24.0f * 1000.0f, 1e-3f);
    assert_true(result->hall_valid);
    for (int i = 0; i < 8; i++) {
        assert_int_equal(result->hall_table[i], 7 - i);
    }
}

/*
 * The three attempts (conf_general.c:563-585): a duty that jumps past the spin-up value leaves the
 * commutation mode alone, which is what makes the reference try again - twice, doubling the minimum
 * speed each time and lowering the integrator's ceiling on the second, which stays through the
 * third.
 */
static void test_motor_id_detect_motor_param_rides_through_three_attempts(void **state) {
    (void)state;
    mock_param_plant_t plant;
    mock_param_plant_init(&plant, 0.6f, 2u);
    motor_id_measure_port_t port = make_param_port(&plant);
    motor_id_app_t app;

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_detect_motor_param(&app, 5.0f, 200.0f, 0.2f), EDGE_OK);

    run_param_ms(&app, &plant, 8000);

    assert_int_equal(app.state, MOTOR_ID_STATE_COMPLETE);
    assert_int_equal(plant.switch_calls, 0);
    assert_int_equal(plant.stage_calls, 3);
    assert_float_equal(plant.staged_min_erpm[0], 200.0f, 1e-6f);
    assert_float_equal(plant.staged_min_erpm[1], 400.0f, 1e-6f);
    assert_float_equal(plant.staged_min_erpm[2], 800.0f, 1e-6f);
    assert_float_equal(plant.staged_limit[0], 50.0f, 1e-6f);
    assert_float_equal(plant.staged_limit[1], 20.0f, 1e-6f);
    assert_float_equal(plant.staged_limit[2], 20.0f, 1e-6f);
    assert_false(plant.staged_delay[1]);
    assert_true(plant.staged_delay[2]);
    /* Each retry lets the motor go first. */
    assert_int_equal(plant.release_calls, 3);
}

/*
 * A duty that never reaches half of the spin-up value and never switches the mode: neither timeout
 * is survived, and the run gives up with the configuration and the timeout put back.
 */
static void test_motor_id_detect_motor_param_fails_when_the_duty_never_rises(void **state) {
    (void)state;
    mock_param_plant_t plant;
    mock_param_plant_init(&plant, 0.0f, 2u);
    motor_id_measure_port_t port = make_param_port(&plant);
    motor_id_app_t app;

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_detect_motor_param(&app, 5.0f, 200.0f, 0.2f), EDGE_OK);

    run_param_ms(&app, &plant, 4000);

    assert_int_equal(app.state, MOTOR_ID_STATE_FAILED);
    assert_false(motor_id_get_result(&app)->valid);
    assert_int_equal(plant.restore_timeout_calls, 1);
    assert_float_equal(plant.iq_set, 0.0f, 1e-6f);
    assert_int_equal(plant.reset_hall_calls, 0);
}

/*
 * A tachometer that never moves: the three watches run out, so only two of the five steps are
 * earned - the spin-up and the slow-down - and the run reports that it did not pass while still
 * handing back the readings it did take.
 */
static void test_motor_id_detect_motor_param_reports_fewer_than_five_steps(void **state) {
    (void)state;
    mock_param_plant_t plant;
    mock_param_plant_init(&plant, 0.01f, 0u);
    motor_id_measure_port_t port = make_param_port(&plant);
    motor_id_app_t app;

    motor_id_construct(&app, EDGE_MOD_MOTOR_ID, 40u, &port);
    assert_int_equal(motor_id_init(&app), EDGE_OK);
    assert_int_equal(motor_id_detect_motor_param(&app, 5.0f, 200.0f, 0.2f), EDGE_OK);

    run_param_ms(&app, &plant, 16000);

    assert_int_equal(app.state, MOTOR_ID_STATE_FAILED);
    const motor_id_result_t *result = motor_id_get_result(&app);
    assert_false(result->valid);
    assert_true(result->hall_valid);
    assert_float_equal(result->int_limit, 100.0f, 1e-6f);
    assert_float_equal(plant.iq_set, 0.0f, 1e-6f);
}

int main(void) {

    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_motor_id_guards_and_unported_procedures),
        cmocka_unit_test(test_motor_id_ramp_rate_is_the_reference_timing),
        cmocka_unit_test(test_motor_id_measures_a_known_resistance),
        cmocka_unit_test(test_motor_id_keeps_the_motor_running_when_asked),
        cmocka_unit_test(test_motor_id_flux_linkage_openloop),
        cmocka_unit_test(test_motor_id_flux_gives_up_when_it_must),
        cmocka_unit_test(test_motor_id_flux_needs_its_ports),
        cmocka_unit_test(test_motor_id_flux_arms),
        cmocka_unit_test(test_motor_id_flux_linkage_sensored),
        cmocka_unit_test(test_motor_id_aborts_on_a_fault),
        cmocka_unit_test(test_motor_id_sample_timeout_publishes_an_invalid_result),
        cmocka_unit_test(test_motor_id_hooks_and_accessor_guards),
        cmocka_unit_test(test_motor_id_measure_inductance),
        cmocka_unit_test(test_motor_id_inductance_pass_count),
        cmocka_unit_test(test_motor_id_inductance_needs_its_ports),
        cmocka_unit_test(test_motor_id_inductance_ready_wait_gives_up),
        cmocka_unit_test(test_motor_id_inductance_aborts_on_a_fault),
        cmocka_unit_test(test_motor_id_inductance_current_scans_the_duty),
        cmocka_unit_test(test_motor_id_measure_r_l_runs_the_whole_sequence),
        cmocka_unit_test(test_motor_id_inductance_failure_paths),
        cmocka_unit_test(test_motor_id_measure_r_l_edge_exits),
        cmocka_unit_test(test_motor_id_gains_match_the_reference),
        cmocka_unit_test(test_motor_id_measure_r_l_imax_runs_to_its_ceiling),
        cmocka_unit_test(test_motor_id_hall_angle_table_matches_the_reference),
        cmocka_unit_test(test_motor_id_hall_majority_matches_the_reference),
        cmocka_unit_test(test_motor_id_hall_accumulate_matches_the_reference),
        cmocka_unit_test(test_motor_id_detect_hall_runs_the_whole_sweep),
        cmocka_unit_test(test_motor_id_spinup_params_match_the_reference),
        cmocka_unit_test(test_motor_id_tacho_advanced_matches_the_reference),
        cmocka_unit_test(test_motor_id_bemf_coupling_k_matches_the_reference),
        cmocka_unit_test(test_motor_id_spinup_passed_matches_the_reference),
        cmocka_unit_test(test_motor_id_detect_motor_param_guards),
        cmocka_unit_test(test_motor_id_detect_motor_param_switches_commutation),
        cmocka_unit_test(test_motor_id_detect_motor_param_rides_through_three_attempts),
        cmocka_unit_test(test_motor_id_detect_motor_param_fails_when_the_duty_never_rises),
        cmocka_unit_test(test_motor_id_detect_motor_param_reports_fewer_than_five_steps),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
