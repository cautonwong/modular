#include "bldc_drive/bldc_drive.h"

#include <math.h>
#include <string.h>

static edge_status_t bldc_drive_poll(edge_module_t *mod) {
    bldc_drive_t *self = (bldc_drive_t *)edge_module_data(mod);

    /* mcpwm.c:2586's update_sensor_mode(), run every cycle of the reference's own sensor thread. */
    self->sensorless_now =
        bldc_sensorless_now(self->config.sensor_mode, self->rpm, self->config.hall_sl_erpm);

    return EDGE_OK;
}

static edge_status_t bldc_drive_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t bldc_drive_power_off(edge_module_t *mod) {
    (void)mod;
    return EDGE_OK;
}

void bldc_drive_construct(bldc_drive_t *self, uint32_t module_id, uint32_t priority,
                          const bldc_drive_config_t *config) {
    if (self == (void *)0) {
        return;
    }

    memset(self, 0, sizeof(*self));
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .poll = bldc_drive_poll,
        .on_event = bldc_drive_on_event,
        .power_off = bldc_drive_power_off,
        .private_data = self,
    };

    if (config != (void *)0) {
        self->config = *config;
    }

    /* The reference's own starting point: no step commutated yet, and the first step is one. */
    self->comm_step = 1;
    self->last_step = 0;
}

edge_status_t bldc_drive_init(bldc_drive_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* mcpwm.c:501, mcpwm_init_hall_table, which the reference calls when a configuration is
     * applied. */
    bldc_build_hall_tables(self->config.hall_table, self->hall_forward, self->hall_reverse);

    self->comm_step = 1;
    self->last_step = 0;
    self->rpm = 0.0f;
    self->has_commutated = false;

    /* The sensorless branch starts where the reference's own state does: nothing integrated, no
     * cycle counted, and the limits computed for a machine at rest. */
    self->comm = (bldc_comm_state_t){0};
    self->pwm_cycles_sum = 0.0f;
    self->last_pwm_cycles_sum = 0.0f;
    self->last_v_diff = 0.0f;
    bldc_rpm_dep_calc(&self->config.rpm_dep, 0.0f, 0.0f, &self->rpm_dep);

    self->sensorless_now =
        bldc_sensorless_now(self->config.sensor_mode, 0.0f, self->config.hall_sl_erpm);
    return EDGE_OK;
}

void bldc_drive_set_rpm(bldc_drive_t *self, float rpm) {
    if (self == (void *)0) {
        return;
    }
    self->rpm = rpm;
}

void bldc_drive_set_hall_port(bldc_drive_t *self, const bldc_hall_port_t *port) {
    if (self == (void *)0) {
        return;
    }
    self->hall = port;
}

void bldc_drive_set_phase_port(bldc_drive_t *self, const bldc_phase_port_t *port) {
    if (self == (void *)0) {
        return;
    }
    self->phase = port;
}

edge_status_t bldc_drive_commutate_hall(bldc_drive_t *self, bool running) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    if (self->hall == (void *)0 || self->hall->read_hall == (void *)0) {
        return EDGE_ENOTSUP;
    }

    /*
     * mcpwm.c:1939 reads mcpwm_read_hall_phase(), and that is the reading looked up in the table
     * half the commanded direction selects - not the pins themselves. This port had been using the
     * raw reading as the step until this slice, which is the same value only while the
     * configuration's table happens to be the identity.
     */
    const uint8_t pins = self->hall->read_hall(self->hall->self);
    const int hall_phase =
        bldc_hall_phase_from_table(self->hall_forward, self->hall_reverse, pins, self->direction);

    bldc_hall_commutation_t decision;
    bldc_hall_commutation(self->comm_step, hall_phase, running, self->has_commutated, &decision);

    self->comm_step = decision.comm_step;
    if (decision.step_changed) {
        /* update_rpm_tacho() runs on a change, and the accessor is what consumes the delta it
         * accumulates - so a commutation is what makes the tachometer move, as there. */
        (void)bldc_drive_get_tacho_delta(self);
    }

    if (!decision.apply) {
        return EDGE_OK;
    }
    if (self->phase == (void *)0 || self->phase->apply_step == (void *)0) {
        return EDGE_ENOTSUP;
    }

    /* set_next_comm_step(comm_step) and commutate(0), which is what the reference's branch does. */
    const edge_status_t status = self->phase->apply_step(self->phase->self, self->comm_step);
    if (status == EDGE_OK) {
        self->has_commutated = true;
    }
    return status;
}

bool bldc_drive_has_commutated(const bldc_drive_t *self) {
    return (self != (void *)0) && self->has_commutated;
}

void bldc_drive_set_direction(bldc_drive_t *self, int direction) {
    if (self == (void *)0) {
        return;
    }
    self->direction = direction;
}

int bldc_drive_get_direction(const bldc_drive_t *self) {
    return (self != (void *)0) ? self->direction : 0;
}

void bldc_drive_hall_detect_reset(bldc_drive_t *self) {
    if (self == (void *)0) {
        return;
    }
    bldc_hall_detect_reset(self->hall_detect_counts);
}

void bldc_drive_hall_detect_sample(bldc_drive_t *self, bool in_first_half) {
    if (self == (void *)0 || self->hall == (void *)0 || self->hall->read_hall == (void *)0) {
        return;
    }

    /*
     * mcpwm.c:1876 counts against the raw reading rather than the looked-up phase - which is why
     * the pins are assembled here rather than taken from the commutation above.
     */
    const uint8_t pins = self->hall->read_hall(self->hall->self);
    const uint8_t reading =
        bldc_hall_phase((pins & 1u) != 0u, (pins & 2u) != 0u, (pins & 4u) != 0u);
    bldc_hall_detect_sample(self->hall_detect_counts, reading, self->comm_step, in_first_half);
}

int bldc_drive_hall_detect_result(bldc_drive_t *self, bool hall_sensor_port, int8_t out[8]) {
    if (self == (void *)0) {
        return -1;
    }
    return bldc_hall_detect_result(self->hall_detect_counts, hall_sensor_port, out);
}

const bldc_hall_detect_counts_t *bldc_drive_hall_detect_counts(const bldc_drive_t *self) {
    /* The cast only adds const to an array type, which the language leaves to the compiler to
     * complain about rather than doing itself. */
    return (self != (void *)0) ? (const bldc_hall_detect_counts_t *)&self->hall_detect_counts
                               : (void *)0;
}

void bldc_drive_set_bemf_port(bldc_drive_t *self, const bldc_bemf_port_t *port) {
    if (self == (void *)0) {
        return;
    }
    self->bemf = port;
}

edge_status_t bldc_drive_commutate_sensorless(bldc_drive_t *self, float duty,
                                              float switching_frequency_now) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* mcpwm.c:1931-1933: a motor that is not running sensorless keeps no integral at all. */
    if (!self->sensorless_now) {
        self->comm.cycle_integrator = 0.0f;
        return EDGE_OK;
    }

    if (self->bemf == (void *)0 || self->bemf->read_phase_difference == (void *)0 ||
        self->bemf->read_v_in == (void *)0) {
        return EDGE_ENOTSUP;
    }

    float v_diff = 0.0f;
    float ph_now_raw = 0.0f;
    const edge_status_t status =
        self->bemf->read_phase_difference(self->bemf->self, &v_diff, &ph_now_raw);
    if (status != EDGE_OK) {
        return status;
    }

    /* mcpwm.c:1880-1882's own noise gate, which is against raw counts. */
    if (fabsf(v_diff) < 10.0f) {
        v_diff = 0.0f;
    }
    self->last_v_diff = v_diff;

    const float v_in = self->bemf->read_v_in(self->bemf->self);
    const float rpm_abs = fabsf(self->rpm);

    /* mcpwm.c:1322-1341 runs every cycle of the reference's own sensor thread, and so here. */
    bldc_rpm_dep_calc(&self->config.rpm_dep, rpm_abs, v_in, &self->rpm_dep);

    const bldc_comm_input_t in = {.v_diff = v_diff,
                                  .pwm_cycles_sum = self->pwm_cycles_sum,
                                  .last_pwm_cycles_sum = self->last_pwm_cycles_sum,
                                  .ph_now_raw = ph_now_raw,
                                  .duty = duty,
                                  .v_in = v_in,
                                  .rpm_abs = rpm_abs,
                                  .switching_frequency_now = switching_frequency_now,
                                  .vdiv_corr = self->config.vdiv_corr,
                                  .comm_mode = self->config.comm_mode,
                                  .has_commutated = self->has_commutated};

    if (bldc_comm_sensorless_step(&in, &self->config.rpm_dep, &self->rpm_dep, &self->comm)) {
        /*
         * commutate(1): the step advances, the tachometer moves with it, the bridge takes the new
         * step, and the commutation cycle's two counts are reset - which is the reference's own
         * order in commutate() before its timer event.
         */
        self->last_pwm_cycles_sum = self->pwm_cycles_sum;
        self->pwm_cycles_sum = 0.0f;
        bldc_drive_advance_step(self, 1);
        (void)bldc_drive_get_tacho_delta(self);

        if (self->phase != (void *)0 && self->phase->apply_step != (void *)0) {
            const edge_status_t applied =
                self->phase->apply_step(self->phase->self, self->comm_step);
            if (applied != EDGE_OK) {
                return applied;
            }
            self->has_commutated = true;
        }
    }

    /*
     * mcpwm.c:1874-1877 samples the detection table from the first half of the commutation cycle,
     * and :1934-1936 counts that cycle up by the switching frequency over the machine's own - both
     * after the decision above, as the reference has them.
     */
    bldc_drive_hall_detect_sample(self, self->pwm_cycles_sum > (self->last_pwm_cycles_sum / 2.0f));
    self->pwm_cycles_sum += self->config.rpm_dep.m_bldc_f_sw_max / switching_frequency_now;

    return EDGE_OK;
}

const bldc_comm_state_t *bldc_drive_comm_state(const bldc_drive_t *self) {
    return (self != (void *)0) ? &self->comm : (void *)0;
}

float bldc_drive_last_v_diff(const bldc_drive_t *self) {
    return (self != (void *)0) ? self->last_v_diff : 0.0f;
}

void bldc_drive_advance_step(bldc_drive_t *self, int steps) {
    if (self == (void *)0) {
        return;
    }

    /*
     * mcpwm.c:2597-2603, which the reference only reaches when the motor is a BLDC one running
     * sensorless; the caller here is the commutation that decides that, and the wrap is the same
     * arithmetic either way.
     */
    self->last_step = self->comm_step - 1;
    self->comm_step = bldc_comm_step_advance(self->comm_step, steps);
}

int bldc_drive_get_comm_step(const bldc_drive_t *self) {
    return (self != (void *)0) ? self->comm_step : 0;
}

int bldc_drive_get_tacho_delta(bldc_drive_t *self) {
    if (self == (void *)0) {
        return 0;
    }

    /*
     * update_rpm_tacho()'s delta, and the step it remembers is the one the last commutation left -
     * the reference keeps that in a static of its own, which is state this aggregate is what
     * carries. Reading it consumes it: the next call compares against this step.
     */
    const int delta = bldc_tacho_step_delta(self->comm_step, self->last_step);
    self->last_step = self->comm_step - 1;
    self->tacho += (int32_t)delta;
    return delta;
}

/*
 * mcpwm.c:2558-2566's own count: the sum of the deltas above, which is the figure the reference's
 * update_rpm_tacho keeps and mc_interface_get_tachometer_value hands out.
 */
int32_t bldc_drive_tacho(const bldc_drive_t *self) {
    return (self != (void *)0) ? self->tacho : 0;
}

/*
 * mcpwm.c:2195-2200, mcpwm_read_reset_avg_cycle_integrator: the average the DELAY mode accumulated
 * over the commutations since this was last called, with the two figures it averages cleared as it
 * is taken - the reference's own division, so a run that accumulated none gives the infinity or the
 * not-a-number that follows from it rather than an excuse not to divide.
 */
float bldc_drive_read_reset_cycle_integrator(bldc_drive_t *self) {
    if (self == (void *)0) {
        return 0.0f;
    }

    const float average = self->comm.cycle_integrator_sum / self->comm.cycle_integrator_iterations;
    self->comm.cycle_integrator_sum = 0.0f;
    self->comm.cycle_integrator_iterations = 0.0f;
    return average;
}

bool bldc_drive_is_sensorless(const bldc_drive_t *self) {
    return (self != (void *)0) && self->sensorless_now;
}

const int8_t *bldc_drive_hall_forward(const bldc_drive_t *self) {
    return (self != (void *)0) ? self->hall_forward : (void *)0;
}

const int8_t *bldc_drive_hall_reverse(const bldc_drive_t *self) {
    return (self != (void *)0) ? self->hall_reverse : (void *)0;
}

edge_module_t *bldc_drive_module(bldc_drive_t *self) {
    return (self != (void *)0) ? &self->module : (void *)0;
}
