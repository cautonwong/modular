#include "throttle/throttle.h"
#include <math.h>
#include <string.h>

float throttle_apply_curve(float raw_in, float deadband, float expo) {
    if (deadband >= 1.0f || deadband < 0.0f) {
        deadband = 0.0f;
    }
    if (expo > 1.0f) {
        expo = 1.0f;
    } else if (expo < -1.0f) {
        expo = -1.0f;
    }

    float sign = raw_in >= 0.0f ? 1.0f : -1.0f;
    float abs_in = fabsf(raw_in);

    if (abs_in <= deadband) {
        return 0.0f;
    }

    /* Rescale range [deadband .. 1.0] -> [0.0 .. 1.0] */
    float x = (abs_in - deadband) / (1.0f - deadband);
    if (x > 1.0f) {
        x = 1.0f;
    }

    /* VESC cubic polynomial curve: y = (1 - expo)*x + expo*x^3 */
    float y = (1.0f - expo) * x + expo * (x * x * x);
    return sign * y;
}

float throttle_apply_ramp(float current_val, float target_val, float ramp_up, float ramp_down,
                          float dt) {
    if (dt <= 0.0f) {
        return current_val;
    }

    if (target_val > current_val) {
        float max_step = (ramp_up > 0.0f) ? (ramp_up * dt) : (target_val - current_val);
        current_val += max_step;
        if (current_val > target_val) {
            current_val = target_val;
        }
    } else if (target_val < current_val) {
        float max_step = (ramp_down > 0.0f) ? (ramp_down * dt) : (current_val - target_val);
        current_val -= max_step;
        if (current_val < target_val) {
            current_val = target_val;
        }
    }

    return current_val;
}

static edge_status_t throttle_poll(edge_module_t *module) {
    throttle_t *self = (throttle_t *)edge_module_data(module);
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    return throttle_step(self);
}

static edge_status_t throttle_on_event(edge_module_t *module, const edge_event_t *event) {
    throttle_t *self = (throttle_t *)edge_module_data(module);
    if (self == (void *)0 || event == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t throttle_power_off(edge_module_t *module) {
    throttle_t *self = (throttle_t *)edge_module_data(module);
    if (self != (void *)0) {
        self->current_output = 0.0f;
        if (self->output != (void *)0 && self->output->set_command != (void *)0) {
            (void)self->output->set_command(self->output->self, 0.0f);
        }
    }
    return EDGE_OK;
}

void throttle_construct(throttle_t *self, uint32_t module_id, uint32_t priority,
                        const throttle_input_port_t *input, const throttle_output_port_t *output,
                        const throttle_curve_config_t *config, float dt_s) {
    if (self == (void *)0) {
        return;
    }

    memset(self, 0, sizeof(*self));

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .budget = 0u,
        .next_due = 0u,
        .poll = throttle_poll,
        .on_event = throttle_on_event,
        .power_off = throttle_power_off,
        .private_data = self,
    };

    self->input = input;
    self->output = output;
    if (config != (void *)0) {
        self->config = *config;
    } else {
        self->config = (throttle_curve_config_t){
            .deadband = 0.05f,
            .expo = 0.0f,
            .ramp_up_rate = 2.0f,
            .ramp_down_rate = 5.0f,
            .min_out = -1.0f,
            .max_out = 1.0f,
        };
    }
    self->dt_s = dt_s > 0.0f ? dt_s : 0.02f;
    self->current_output = 0.0f;
    self->last_raw_input = 0.0f;
}

edge_status_t throttle_init(throttle_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    self->current_output = 0.0f;
    self->last_raw_input = 0.0f;
    return EDGE_OK;
}

edge_status_t throttle_deinit(throttle_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t throttle_step(throttle_t *self) {
    if (self == (void *)0 || self->input == (void *)0 || self->input->read_raw == (void *)0) {
        return EDGE_EINVAL;
    }

    float raw = 0.0f;
    edge_status_t status = self->input->read_raw(self->input->self, &raw);
    if (status != EDGE_OK) {
        return status;
    }

    self->last_raw_input = raw;

    /* 1. Apply curve and deadband */
    float curved = throttle_apply_curve(raw, self->config.deadband, self->config.expo);

    /* 2. Apply clamp limits */
    if (curved > self->config.max_out) {
        curved = self->config.max_out;
    } else if (curved < self->config.min_out) {
        curved = self->config.min_out;
    }

    /* 3. Apply rate limiting ramp */
    float next_out = throttle_apply_ramp(self->current_output, curved, self->config.ramp_up_rate,
                                         self->config.ramp_down_rate, self->dt_s);
    self->current_output = next_out;

    /* 4. Emit to output port */
    if (self->output != (void *)0 && self->output->set_command != (void *)0) {
        return self->output->set_command(self->output->self, next_out);
    }

    return EDGE_OK;
}

edge_module_t *throttle_module(throttle_t *self) {
    if (self == (void *)0) {
        return (void *)0;
    }
    return &self->module;
}

float throttle_get_output(const throttle_t *self) {
    return self != (void *)0 ? self->current_output : 0.0f;
}
