#include "glue.h"
#include <string.h>

/* Storage Port Adaptors */
static edge_status_t storage_read(void *self, uint32_t offset, uint8_t *buf, size_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (offset + len > sizeof(s->flash_mem)) {
        return EDGE_EINVAL;
    }
    memcpy(buf, s->flash_mem + offset, len);
    return EDGE_OK;
}

static edge_status_t storage_write(void *self, uint32_t offset, const uint8_t *buf, size_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (offset + len > sizeof(s->flash_mem)) {
        return EDGE_EINVAL;
    }
    memcpy(s->flash_mem + offset, buf, len);
    return EDGE_OK;
}

static edge_status_t storage_erase(void *self, uint32_t offset, size_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (offset + len > sizeof(s->flash_mem)) {
        return EDGE_EINVAL;
    }
    memset(s->flash_mem + offset, 0xFF, len);
    return EDGE_OK;
}

void vesc_host_make_storage_port(motor_config_storage_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (motor_config_storage_port_t){
        .read = storage_read,
        .write = storage_write,
        .erase = storage_erase,
        .self = state,
    };
}

/* Stream TX Port Adaptors */
static edge_status_t stream_write(void *self, const uint8_t *data, size_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (len > sizeof(s->stream_tx_buf)) {
        return EDGE_ENOSPC;
    }
    memcpy(s->stream_tx_buf, data, len);
    s->stream_tx_len = len;
    return EDGE_OK;
}

void vesc_host_make_stream_tx_port(edge_stream_tx_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (edge_stream_tx_port_t){
        .write = stream_write,
        .self = state,
    };
}

/* VESC Motor Provider Port Adaptors */
static edge_status_t motor_get_values(void *self, vesc_values_t *out_val) {
    foc_core_t *foc = (foc_core_t *)self;
    if (!foc || !out_val) {
        return EDGE_EINVAL;
    }
    foc_telemetry_t telem;
    foc_core_get_telemetry(foc, &telem);

    memset(out_val, 0, sizeof(*out_val));
    out_val->temp_mos = telem.fet_temp_c;
    out_val->temp_motor = telem.fet_temp_c;
    out_val->current_motor = telem.current_q;
    out_val->id = telem.current_d;
    out_val->iq = telem.current_q;
    out_val->v_in = telem.v_bus;
    out_val->rpm = telem.speed_rpm;
    out_val->duty_now = telem.duty_now;
    out_val->fault_code = telem.faults;
    return EDGE_OK;
}

static edge_status_t motor_set_duty(void *self, float duty) {
    foc_core_t *foc = (foc_core_t *)self;
    return foc_core_set_duty(foc, duty);
}

static edge_status_t motor_set_current(void *self, float current) {
    foc_core_t *foc = (foc_core_t *)self;
    return foc_core_set_current(foc, current, 0.0f);
}

static edge_status_t motor_set_current_brake(void *self, float current) {
    foc_core_t *foc = (foc_core_t *)self;
    return foc_core_set_current(foc, -current, 0.0f);
}

static edge_status_t motor_set_rpm(void *self, float rpm) {
    (void)self;
    (void)rpm;
    return EDGE_OK;
}

static edge_status_t motor_set_pos(void *self, float pos) {
    (void)self;
    (void)pos;
    return EDGE_OK;
}

void vesc_host_make_motor_provider_port(vesc_motor_provider_port_t *out, foc_core_t *foc) {
    if (!out || !foc) {
        return;
    }
    *out = (vesc_motor_provider_port_t){
        .get_values = motor_get_values,
        .set_duty = motor_set_duty,
        .set_current = motor_set_current,
        .set_current_brake = motor_set_current_brake,
        .set_rpm = motor_set_rpm,
        .set_pos = motor_set_pos,
        .self = foc,
    };
}

/* Inverter, Current, Rotor Ports */
static edge_status_t inverter_set_duty(void *self, float duty_a, float duty_b, float duty_c) {
    (void)self;
    (void)duty_a;
    (void)duty_b;
    (void)duty_c;
    return EDGE_OK;
}

static edge_status_t inverter_set_phase_state(void *self, bool enable) {
    (void)self;
    (void)enable;
    return EDGE_OK;
}

void vesc_host_make_inverter_port(foc_inverter_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (foc_inverter_port_t){
        .set_duty = inverter_set_duty,
        .set_phase_state = inverter_set_phase_state,
        .self = state,
    };
}

static edge_status_t current_read_currents(void *self, float *ia, float *ib, float *ic) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (ia)
        *ia = s->vmotor.ia;
    if (ib)
        *ib = s->vmotor.ib;
    if (ic)
        *ic = s->vmotor.ic;
    return EDGE_OK;
}

static edge_status_t current_read_vbus(void *self, float *v_bus) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (v_bus)
        *v_bus = s->v_bus > 0.0f ? s->v_bus : 24.0f;
    return EDGE_OK;
}

void vesc_host_make_current_port(foc_current_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (foc_current_port_t){
        .read_currents = current_read_currents,
        .read_vbus = current_read_vbus,
        .self = state,
    };
}

static edge_status_t rotor_read_angle(void *self, float *angle_rad, float *rpm) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (angle_rad)
        *angle_rad = s->vmotor.rotor_angle_rad;
    if (rpm)
        *rpm = s->vmotor.rotor_speed_rad_s * (60.0f / (2.0f * 3.14159265f));
    return EDGE_OK;
}

void vesc_host_make_rotor_port(foc_rotor_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (foc_rotor_port_t){
        .read_angle = rotor_read_angle,
        .self = state,
    };
}
