/* clang-format off */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

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

    /* The three procedures that are not ported say so instead of inventing a measurement. */
    assert_int_equal(motor_id_measure_r_l(&app), EDGE_ENOTSUP);
    assert_int_equal(motor_id_measure_flux_linkage(&app), EDGE_ENOTSUP);
    assert_int_equal(motor_id_detect_hall(&app), EDGE_ENOTSUP);
    assert_int_equal(motor_id_measure_r_l(NULL), EDGE_EINVAL);
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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_motor_id_guards_and_unported_procedures),
        cmocka_unit_test(test_motor_id_ramp_rate_is_the_reference_timing),
        cmocka_unit_test(test_motor_id_measures_a_known_resistance),
        cmocka_unit_test(test_motor_id_keeps_the_motor_running_when_asked),
        cmocka_unit_test(test_motor_id_aborts_on_a_fault),
        cmocka_unit_test(test_motor_id_sample_timeout_publishes_an_invalid_result),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
