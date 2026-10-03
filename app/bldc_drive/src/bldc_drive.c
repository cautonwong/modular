#include "bldc_drive/bldc_drive.h"

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
    return delta;
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
