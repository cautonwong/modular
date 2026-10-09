#include "zmk_sensors/sensors.h"

static edge_status_t zmk_sensors_poll(edge_module_t *module) {
    const zmk_sensors_app_t *self = (const zmk_sensors_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_sensors_power_off(edge_module_t *module) {
    zmk_sensors_app_t *self = (zmk_sensors_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (uint8_t i = 0; i < self->sensor_count; i++) {
        self->sensors[i].pulse_accumulator = 0;
    }
    return EDGE_OK;
}

void zmk_sensors_construct(zmk_sensors_app_t *self, uint32_t module_id, uint32_t priority,
                           const zmk_sensors_behavior_if_t *behavior_port) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = zmk_sensors_poll,
        .on_event = NULL,
        .power_off = zmk_sensors_power_off,
        .private_data = self,
    };
    self->behavior_port = behavior_port;
}

edge_status_t zmk_sensors_init(zmk_sensors_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (uint8_t i = 0; i < self->sensor_count; i++) {
        self->sensors[i].pulse_accumulator = 0;
        self->sensors[i].pin_a_state = 0;
        self->sensors[i].pin_b_state = 0;
    }
    return EDGE_OK;
}

// cppcheck-suppress constParameterPointer
edge_status_t zmk_sensors_shutdown(zmk_sensors_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t zmk_sensors_add_encoder(zmk_sensors_app_t *self, uint8_t pulses_per_detent,
                                      zmk_sensor_binding_t binding, uint8_t *out_sensor_id) {
    if (self == NULL || self->sensor_count >= ZMK_MAX_SENSORS) {
        return EDGE_EINVAL;
    }
    uint8_t id = self->sensor_count++;
    self->sensors[id].pulses_per_detent = (pulses_per_detent == 0) ? 4 : pulses_per_detent;
    self->sensors[id].binding = binding;
    self->sensors[id].pulse_accumulator = 0;

    if (out_sensor_id != NULL) {
        *out_sensor_id = id;
    }
    return EDGE_OK;
}

edge_status_t zmk_sensors_process_step(zmk_sensors_app_t *self, uint8_t sensor_id, int8_t steps,
                                       uint32_t timestamp_ms) {
    if (self == NULL || sensor_id >= self->sensor_count || steps == 0) {
        return EDGE_EINVAL;
    }
    const zmk_encoder_state_t *st = &self->sensors[sensor_id];
    if (self->behavior_port == NULL || self->behavior_port->invoke_binding == NULL) {
        return EDGE_OK;
    }

    if (steps > 0) {
        for (int8_t i = 0; i < steps; i++) {
            self->behavior_port->invoke_binding(self->behavior_port->self, st->binding.behavior_id,
                                                st->binding.cw_param1, 0, true, timestamp_ms);
            self->behavior_port->invoke_binding(self->behavior_port->self, st->binding.behavior_id,
                                                st->binding.cw_param1, 0, false, timestamp_ms);
        }
    } else {
        int8_t count = -steps;
        for (int8_t i = 0; i < count; i++) {
            self->behavior_port->invoke_binding(self->behavior_port->self, st->binding.behavior_id,
                                                st->binding.ccw_param1, 0, true, timestamp_ms);
            self->behavior_port->invoke_binding(self->behavior_port->self, st->binding.behavior_id,
                                                st->binding.ccw_param1, 0, false, timestamp_ms);
        }
    }
    return EDGE_OK;
}

/* 2-bit Gray code quadrature decoding */
edge_status_t zmk_sensors_process_encoder_pulse(zmk_sensors_app_t *self, uint8_t sensor_id,
                                                uint8_t pin_a, uint8_t pin_b,
                                                uint32_t timestamp_ms) {
    if (self == NULL || sensor_id >= self->sensor_count) {
        return EDGE_EINVAL;
    }
    zmk_encoder_state_t *st = &self->sensors[sensor_id];
    uint8_t old_state = (st->pin_a_state << 1) | st->pin_b_state;
    uint8_t new_state = ((pin_a & 1) << 1) | (pin_b & 1);

    st->pin_a_state = pin_a & 1;
    st->pin_b_state = pin_b & 1;

    if (old_state == new_state) {
        return EDGE_OK;
    }

    /* Quadrature lookup transition delta */
    static const int8_t quad_table[4][4] = {
        {0, 1, -1, 0},
        {-1, 0, 0, 1},
        {1, 0, 0, -1},
        {0, -1, 1, 0},
    };

    int8_t delta = quad_table[old_state][new_state];
    st->pulse_accumulator += delta;

    if (st->pulse_accumulator >= (int8_t)st->pulses_per_detent) {
        st->pulse_accumulator = 0;
        return zmk_sensors_process_step(self, sensor_id, 1, timestamp_ms);
    } else if (st->pulse_accumulator <= -(int8_t)st->pulses_per_detent) {
        st->pulse_accumulator = 0;
        return zmk_sensors_process_step(self, sensor_id, -1, timestamp_ms);
    }

    return EDGE_OK;
}
