#include "glue.h"
#include <stdio.h>
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

/* PPM Port Adaptor */
static edge_status_t ppm_read_pulse(void *self, float *pulse_us) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    *pulse_us = s->ppm_pulse_us > 0.0f ? s->ppm_pulse_us : 1500.0f;
    return EDGE_OK;
}

static bool ppm_is_signal(void *self) {
    (void)self;
    return true;
}

void vesc_host_make_ppm_port(ppm_receiver_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (ppm_receiver_port_t){
        .self = state,
        .read_pulse_us = ppm_read_pulse,
        .is_signal_present = ppm_is_signal,
    };
}

/* ADC Port Adaptor */
static edge_status_t adc_read_throttle(void *self, float *v) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    *v = s->adc_throttle_v > 0.0f ? s->adc_throttle_v : 1.0f;
    return EDGE_OK;
}

static edge_status_t adc_read_brake(void *self, float *v) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    *v = s->adc_brake_v;
    return EDGE_OK;
}

static bool adc_read_button(void *self, uint8_t idx) {
    (void)self;
    (void)idx;
    return false;
}

void vesc_host_make_adc_port(adc_input_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (adc_input_port_t){
        .self = state,
        .read_throttle_v = adc_read_throttle,
        .read_brake_v = adc_read_brake,
        .read_button = adc_read_button,
    };
}

/* CAN Port Adaptor */
static edge_status_t can_send(void *self, uint32_t can_id, const uint8_t *data, uint8_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    s->last_can_id = can_id;
    s->last_can_len = len;
    for (uint8_t i = 0; i < len && i < 8; i++) {
        s->last_can_data[i] = data[i];
    }
    return EDGE_OK;
}

static edge_status_t can_recv(void *self, uint32_t *can_id, uint8_t *data, uint8_t *len) {
    (void)self;
    (void)can_id;
    (void)data;
    (void)len;
    return EDGE_ENOENT;
}

void vesc_host_make_can_port(vesc_can_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (vesc_can_port_t){
        .self = state,
        .send_frame = can_send,
        .receive_frame = can_recv,
    };
}

/* Motor ID Measure & Control Port Adaptors */
static edge_status_t id_get_currents(void *self, float *ia, float *ib) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    *ia = s->vmotor.ia;
    *ib = s->vmotor.ib;
    return EDGE_OK;
}

static edge_status_t id_get_vbus(void *self, float *v_bus) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    *v_bus = s->v_bus > 0.0f ? s->v_bus : 24.0f;
    return EDGE_OK;
}

static uint8_t id_get_hall(void *self) {
    (void)self;
    return 1;
}

static float id_get_rotor_angle(void *self) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    return s->vmotor.rotor_angle_rad;
}

void vesc_host_make_motor_id_measure_port(motor_id_measure_port_t *out,
                                          vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (motor_id_measure_port_t){
        .self = state,
        .get_currents = id_get_currents,
        .get_vbus = id_get_vbus,
        .get_hall = id_get_hall,
        .get_rotor_angle = id_get_rotor_angle,
    };
}

static edge_status_t id_set_v_ab(void *self, float va, float vb) {
    (void)self;
    (void)va;
    (void)vb;
    return EDGE_OK;
}

static edge_status_t id_set_duty(void *self, float da, float db, float dc) {
    (void)self;
    (void)da;
    (void)db;
    (void)dc;
    return EDGE_OK;
}

static edge_status_t id_set_openloop(void *self, float angle, float curr) {
    (void)self;
    (void)angle;
    (void)curr;
    return EDGE_OK;
}

static edge_status_t id_stop(void *self) {
    (void)self;
    return EDGE_OK;
}

void vesc_host_make_motor_id_control_port(motor_id_control_port_t *out,
                                          vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (motor_id_control_port_t){
        .self = state,
        .set_voltage_alpha_beta = id_set_v_ab,
        .set_pwm_duty = id_set_duty,
        .set_openloop_angle = id_set_openloop,
        .stop_inverter = id_stop,
    };
}

/* Nunchuk Port Adaptor */
static edge_status_t nunchuk_read(void *self, uint8_t *js_x, uint8_t *js_y, int16_t *acc_x,
                                  int16_t *acc_y, int16_t *acc_z, bool *btn_c, bool *btn_z) {
    (void)self;
    if (js_x)
        *js_x = 128;
    if (js_y)
        *js_y = 128;
    if (acc_x)
        *acc_x = 0;
    if (acc_y)
        *acc_y = 0;
    if (acc_z)
        *acc_z = 0;
    if (btn_c)
        *btn_c = false;
    if (btn_z)
        *btn_z = false;
    return EDGE_OK;
}

void vesc_host_make_nunchuk_port(nunchuk_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (nunchuk_port_t){
        .self = state,
        .read_data = nunchuk_read,
    };
}

/* PAS Port Adaptor */
static edge_status_t pas_read_cadence(void *self, float *rpm) {
    (void)self;
    if (rpm)
        *rpm = 60.0f;
    return EDGE_OK;
}

static edge_status_t pas_read_torque(void *self, float *nm) {
    (void)self;
    if (nm)
        *nm = 15.0f;
    return EDGE_OK;
}

void vesc_host_make_pas_port(pas_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (pas_port_t){
        .self = state,
        .read_cadence_rpm = pas_read_cadence,
        .read_torque_nm = pas_read_torque,
    };
}

/* Balance Port Adaptor */
static edge_status_t balance_read_att(void *self, float *pitch, float *roll, float *gp, float *gr,
                                      bool *sw1, bool *sw2) {
    (void)self;
    if (pitch)
        *pitch = 0.0f;
    if (roll)
        *roll = 0.0f;
    if (gp)
        *gp = 0.0f;
    if (gr)
        *gr = 0.0f;
    if (sw1)
        *sw1 = false;
    if (sw2)
        *sw2 = false;
    return EDGE_OK;
}

void vesc_host_make_balance_port(balance_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (balance_port_t){
        .self = state,
        .read_attitude = balance_read_att,
    };
}

/* Terminal Stream & System Ports */
static edge_status_t term_write_str(void *self, const char *str) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    strncpy(s->terminal_tx_buf, str, sizeof(s->terminal_tx_buf) - 1);
    return EDGE_OK;
}

void vesc_host_make_terminal_stream_port(terminal_stream_port_t *out,
                                         vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (terminal_stream_port_t){
        .self = state,
        .write_string = term_write_str,
    };
}

static edge_status_t term_get_stats(void *self, float *rpm, float *iq, float *v_bus, float *temp,
                                    uint32_t *faults) {
    foc_core_t *foc = (foc_core_t *)self;
    if (!foc) {
        return EDGE_EINVAL;
    }
    foc_telemetry_t telem;
    foc_core_get_telemetry(foc, &telem);
    if (rpm)
        *rpm = telem.speed_rpm;
    if (iq)
        *iq = telem.current_q;
    if (v_bus)
        *v_bus = telem.v_bus;
    if (temp)
        *temp = telem.fet_temp_c;
    if (faults)
        *faults = telem.faults;
    return EDGE_OK;
}

void vesc_host_make_terminal_system_port(terminal_system_port_t *out, foc_core_t *foc) {
    if (!out || !foc) {
        return;
    }
    *out = (terminal_system_port_t){
        .self = foc,
        .get_stats = term_get_stats,
    };
}

/* BMS CAN Port */
static edge_status_t bms_send_can(void *self, uint32_t id, const uint8_t *data, uint8_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    s->last_can_id = id;
    s->last_can_len = len > 8 ? 8 : len;
    if (data) {
        memcpy(s->last_can_data, data, s->last_can_len);
    }
    return EDGE_OK;
}

void vesc_host_make_bms_can_port(bms_can_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (bms_can_port_t){
        .self = state,
        .send_can_msg = bms_send_can,
    };
}
