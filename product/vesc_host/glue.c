#include "glue.h"
#include "flash/flash.h"
#include "motor_config/motor_config.h"
#include "vesc_can/vesc_can.h"
#include "vesc_terminal/vesc_terminal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Flash Sector Port Adaptors: the EEPROM emulation's view of the flash, which is whole-sector
 * erasure and half-word programming. */
static edge_status_t sector_read(void *self, uint32_t offset, uint8_t *buf, size_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (offset + len > sizeof(s->flash_mem)) {
        return EDGE_EINVAL;
    }
    memcpy(buf, s->flash_mem + offset, len);
    return EDGE_OK;
}

static edge_status_t sector_write_halfword(void *self, uint32_t offset, uint16_t value) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (offset + 2u > sizeof(s->flash_mem)) {
        return EDGE_EINVAL;
    }
    /* Flash can only clear bits. Programming a value that would raise one is the error a real
     * part reports, and it is what catches a record being written over itself. */
    const uint16_t existing =
        (uint16_t)((uint16_t)s->flash_mem[offset] | ((uint16_t)s->flash_mem[offset + 1u] << 8));
    if ((value & existing) != value) {
        return EDGE_EBUSY;
    }
    s->flash_mem[offset] = (uint8_t)(value & 0xFFu);
    s->flash_mem[offset + 1u] = (uint8_t)(value >> 8);
    return EDGE_OK;
}

static edge_status_t sector_erase(void *self, uint32_t offset, size_t len) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    /* Granularity: whole sectors, nothing smaller. */
    if (len != FLASH_EMUL_PAGE_SIZE || offset + len > sizeof(s->flash_mem)) {
        return EDGE_EINVAL;
    }
    memset(s->flash_mem + offset, 0xFF, len);
    return EDGE_OK;
}

void vesc_host_make_flash_sector_port(flash_sector_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (flash_sector_port_t){
        .read = sector_read,
        .write_halfword = sector_write_halfword,
        .erase_sector = sector_erase,
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
static edge_status_t motor_get_values(void *self, uint32_t mask, vesc_values_t *out_val) {
    foc_core_t *foc = (foc_core_t *)self;
    if (!foc || !out_val) {
        return EDGE_EINVAL;
    }

    foc_telemetry_t telem;
    foc_core_get_telemetry(foc, &telem);
    memset(out_val, 0, sizeof(*out_val));

    /*
     * Read-and-reset channels: only the ones the peer asked for are consumed, so a
     * selective read cannot shorten another field's averaging window.
     */
    uint32_t channels = 0u;
    if (mask & (1u << 2)) {
        channels |= FOC_AVG_MOTOR_CURRENT;
    }
    if (mask & (1u << 3)) {
        channels |= FOC_AVG_INPUT_CURRENT;
    }
    if (mask & (1u << 4)) {
        channels |= FOC_AVG_ID;
    }
    if (mask & (1u << 5)) {
        channels |= FOC_AVG_IQ;
    }
    if (mask & (1u << 19)) {
        channels |= FOC_AVG_VD;
    }
    if (mask & (1u << 20)) {
        channels |= FOC_AVG_VQ;
    }

    foc_averages_t avg;
    foc_core_read_reset_averages(foc, channels, &avg);

    if (mask & (1u << 0)) {
        out_val->temp_mos = telem.fet_temp_c;
    }
    if (mask & (1u << 1)) {
        out_val->temp_motor = telem.motor_temp_c;
    }
    if (mask & (1u << 2)) {
        out_val->current_motor = avg.motor_current;
    }
    if (mask & (1u << 3)) {
        out_val->current_in = telem.current_in;
    }
    if (mask & (1u << 9)) {
        out_val->amp_hours = telem.amp_hours;
    }
    if (mask & (1u << 10)) {
        out_val->amp_hours_charged = telem.amp_hours_charged;
    }
    if (mask & (1u << 11)) {
        out_val->watt_hours = telem.watt_hours;
    }
    if (mask & (1u << 12)) {
        out_val->watt_hours_charged = telem.watt_hours_charged;
    }
    if (mask & (1u << 4)) {
        out_val->id = avg.id;
    }
    if (mask & (1u << 5)) {
        out_val->iq = avg.iq;
    }
    if (mask & (1u << 6)) {
        out_val->duty_now = telem.duty_now;
    }
    if (mask & (1u << 7)) {
        out_val->rpm = telem.speed_rpm;
    }
    if (mask & (1u << 8)) {
        out_val->v_in = telem.v_bus;
    }
    if (mask & (1u << 13)) {
        out_val->tachometer = telem.tachometer;
    }
    if (mask & (1u << 14)) {
        out_val->tachometer_abs = telem.tachometer_abs;
    }
    if (mask & (1u << 15)) {
        out_val->fault_code = telem.faults;
    }
    if (mask & (1u << 16)) {
        out_val->pid_pos_now = telem.rotor_angle_rad;
    }
    if (mask & (1u << 17)) {
        out_val->controller_id = 1u;
    }
    if (mask & (1u << 19)) {
        out_val->vd = avg.vd;
    }
    if (mask & (1u << 20)) {
        out_val->vq = avg.vq;
    }
    /*
     * Not filled, and not silently faked: bit 18 (three MOSFET temperatures) has no
     * NTC source, and bit 21 (timeout / kill switch) belongs to apps this product
     * does not wire. They stay 0.
     */
    return EDGE_OK;
}

/*
 * DIR_MULT, reference mc_interface.c:52. The reference applies it per command at the
 * mc_interface_set_* entry points - set_current, set_brake_current, set_duty,
 * set_pid_speed and the internal position setpoint all multiply by it, while
 * set_handbrake deliberately does not - so it belongs at this layer, once per
 * adapter, rather than inside foc_core.
 */
static float motor_dir_mult(const foc_core_t *foc) {
    return foc->config.m_invert_direction ? -1.0f : 1.0f;
}

static edge_status_t motor_set_duty(void *self, float duty) {
    foc_core_t *foc = (foc_core_t *)self;
    return foc_core_set_duty(foc, motor_dir_mult(foc) * duty);
}

static edge_status_t motor_set_current(void *self, float current) {
    foc_core_t *foc = (foc_core_t *)self;
    return foc_core_set_current(foc, motor_dir_mult(foc) * current, 0.0f);
}

static edge_status_t motor_set_current_rel(void *self, float rel) {
    foc_core_t *foc = (foc_core_t *)self;
    /* DIR_MULT applies because the reference routes this through
     * mc_interface_set_current (mc_interface.c:746). */
    return foc_core_set_current_rel(foc, motor_dir_mult(foc) * rel);
}

static edge_status_t motor_set_handbrake(void *self, float current) {
    /* No DIR_MULT: the reference's mc_interface_set_handbrake does not apply it, unlike
     * current, brake, duty and pid_speed (mc_interface.c:777-800). */
    return foc_core_set_handbrake((foc_core_t *)self, current);
}

static edge_status_t motor_set_current_brake(void *self, float current) {
    foc_core_t *foc = (foc_core_t *)self;
    /*
     * Known divergence, recorded rather than guessed at: the reference's brake command
     * does NOT negate - it sets CONTROL_MODE_CURRENT_BRAKE and stores DIR_MULT * current
     * (mcpwm_foc.c:828-834). This port has no brake control mode (its FOC_STATE_* is the
     * control mode), so braking is approximated with a negative current. Flipping only
     * the sign here would change how the product brakes without the mode that gives that
     * sign its meaning.
     */
    return foc_core_set_current(foc, -current, 0.0f);
}

static edge_status_t motor_set_rpm(void *self, float rpm) {
    foc_core_t *foc = (foc_core_t *)self;
    return foc_core_set_rpm(foc, motor_dir_mult(foc) * rpm);
}

static edge_status_t motor_set_pos(void *self, float pos) {
    return foc_core_set_pos((foc_core_t *)self, pos);
}

/*
 * The reference samples both NTCs in its ADC interrupt handler and filters them there
 * (mc_interface.c:2266 for the FET, :2325-2331 for the motor); the FOC only ever reads the
 * filtered values. UTILS_LP_FAST is v -= f * (v - x), kept in that form rather than the
 * algebraically equal v += f * (x - v) because the rounding is not the same.
 */
void vesc_host_sample_temperatures(vesc_host_glue_state_t *state, foc_core_t *foc) {
    if (state == (void *)0 || foc == (void *)0) {
        return;
    }

    /*
     * A reading that cannot be a temperature is substituted rather than filtered: the
     * reference's own comment says a value that walks into the filter never comes back out,
     * so it prefers nonsense over a temperature channel that is permanently wrong.
     */
    float temp_motor = state->motor_temp_raw_c;
    if (isnan(temp_motor) || isinf(temp_motor) || temp_motor > 600.0f || temp_motor < -200.0f) {
        temp_motor = -100.0f;
    }

    state->motor_temp_c -= VESC_HOST_MOTOR_TEMP_LPF * (state->motor_temp_c - temp_motor);
    state->fet_temp_c -= 0.1 * (state->fet_temp_c - state->fet_temp_raw_c);

    foc_core_set_motor_temperature(foc, state->motor_temp_c);
    foc_core_set_fet_temperature(foc, state->fet_temp_c);
}

static edge_status_t motor_get_stats(void *self, vesc_stats_t *out_val) {
    foc_core_t *foc = (foc_core_t *)self;
    if (!foc || !out_val) {
        return EDGE_EINVAL;
    }

    foc_stats_t st;
    foc_core_get_stats(foc, &st);
    memset(out_val, 0, sizeof(*out_val));

    out_val->speed_avg = st.speed_avg;
    out_val->speed_max = st.speed_max;
    out_val->power_avg = st.power_avg;
    out_val->power_max = st.power_max;
    out_val->current_avg = st.current_avg;
    out_val->current_max = st.current_max;
    out_val->temp_mos_avg = st.temp_mos_avg;
    out_val->temp_mos_max = st.temp_mos_max;
    out_val->temp_motor_avg = st.temp_motor_avg;
    out_val->temp_motor_max = st.temp_motor_max;
    /* count_time still needs a clock; foc_core is not given one yet. */
    return EDGE_OK;
}

static edge_status_t motor_reset_stats(void *self) {
    foc_core_stats_reset((foc_core_t *)self);
    return EDGE_OK;
}

static edge_status_t config_get_mcconf(void *self, uint8_t *out, size_t buf_size, size_t *out_len) {
    motor_config_t *cfg = (motor_config_t *)self;
    return motor_config_serialize_mc(motor_config_get_mc(cfg), out, buf_size, out_len);
}

static edge_status_t config_set_mcconf(void *self, const uint8_t *in, size_t len) {
    return motor_config_apply_mc_stream((motor_config_t *)self, in, len);
}

static edge_status_t config_get_appconf(void *self, uint8_t *out, size_t buf_size,
                                        size_t *out_len) {
    motor_config_t *cfg = (motor_config_t *)self;
    return motor_config_serialize_app(motor_config_get_app(cfg), out, buf_size, out_len);
}

static edge_status_t config_set_appconf(void *self, const uint8_t *in, size_t len) {
    return motor_config_apply_app_stream((motor_config_t *)self, in, len);
}

static edge_status_t config_get_mcconf_default(void *self, uint8_t *out, size_t buf_size,
                                               size_t *out_len) {
    return motor_config_serialize_mc_defaults((motor_config_t *)self, out, buf_size, out_len);
}

static edge_status_t config_get_appconf_default(void *self, uint8_t *out, size_t buf_size,
                                                size_t *out_len) {
    (void)self;
    return motor_config_serialize_app_defaults(out, buf_size, out_len);
}

static edge_status_t config_set_appconf_nostore(void *self, const uint8_t *in, size_t len) {
    return motor_config_apply_app_stream_nostore((motor_config_t *)self, in, len);
}

/*
 * COMM_GET_VALUES_SETUP's source: the reference's mc_interface_get_setup_values() together with
 * mc_interface_get_battery_level(). Fields this product has no source for are named zeros with
 * their reasons rather than guesses:
 *   temp_motor    the filtered motor NTC reading the product's sampler delivers
 *   odometer_m    needs a persisted counter, and the reference's own accumulation site has not
 *                 been located yet; inventing one would be inventing the number
 *   uptime_ms     this module is given no clock
 *   num_vescs     stays 1: the reference aggregates unexpired CAN status frames, and this
 *                 product has no CAN status receive path
 *   controller_id 1 until the app configuration reaches this adapter
 * The battery level uses the bus voltage where the reference filters a slower input voltage;
 * this port has no such filter, which is recorded in adr-conformance.md.
 */
static edge_status_t motor_get_setup_values(void *self, vesc_setup_values_t *out) {
    foc_core_t *foc = (foc_core_t *)self;
    if (foc == (void *)0 || out == (void *)0) {
        return EDGE_EINVAL;
    }

    foc_telemetry_t telem;
    foc_core_get_telemetry(foc, &telem);
    memset(out, 0, sizeof(*out));

    const float tacho_scale =
        (foc->config.si_wheel_diameter * (float)M_PI) /
        (3.0f * (float)foc->config.si_motor_poles * foc->config.si_gear_ratio);

    out->temp_mos = telem.fet_temp_c;
    out->temp_motor = telem.motor_temp_c;
    out->current_tot = telem.current_abs;
    out->current_in_tot = telem.current_in;
    out->duty_now = telem.duty_now;
    /* The reference's mc_interface_get_rpm() is electrical rpm. */
    out->rpm = telem.speed_rpm * ((float)foc->config.si_motor_poles / 2.0f);
    out->speed_m_s = foc->speed_m_s;
    out->v_in = telem.v_bus;
    out->ah_tot = telem.amp_hours;
    out->ah_charge_tot = telem.amp_hours_charged;
    out->wh_tot = telem.watt_hours;
    out->wh_charge_tot = telem.watt_hours_charged;
    out->distance_m = (float)telem.tachometer * tacho_scale;
    out->distance_abs_m = (float)telem.tachometer_abs * tacho_scale;
    out->pid_pos_deg = telem.rotor_angle_rad * (180.0f / (float)M_PI);
    out->fault = (uint8_t)telem.faults;
    out->controller_id = 1u;
    out->num_vescs = 1u;
    out->battery_level =
        foc_battery_level(foc->config.si_battery_type, foc->config.si_battery_cells,
                          foc->config.si_battery_ah, telem.v_bus, &out->wh_batt_left);
    out->odometer_m = 0u;
    out->uptime_ms = 0u;
    return EDGE_OK;
}

void vesc_host_make_config_port(vesc_config_provider_port_t *out, motor_config_t *cfg) {
    if (out == (void *)0) {
        return;
    }

    *out = (vesc_config_provider_port_t){
        .get_mcconf = config_get_mcconf,
        .set_mcconf = config_set_mcconf,
        .get_appconf = config_get_appconf,
        .set_appconf = config_set_appconf,
        .get_mcconf_default = config_get_mcconf_default,
        .get_appconf_default = config_get_appconf_default,
        .set_appconf_nostore = config_set_appconf_nostore,
        .self = cfg,
    };
}

static edge_status_t var_read(void *self, uint16_t index, uint16_t *value) {
    flash_emul_t *emul = (flash_emul_t *)self;
    return flash_emul_read(emul, (uint16_t)(VESC_HOST_MCCONF_BASE + index), value);
}

static edge_status_t var_write(void *self, uint16_t index, uint16_t value) {
    flash_emul_t *emul = (flash_emul_t *)self;
    return flash_emul_write(emul, (uint16_t)(VESC_HOST_MCCONF_BASE + index), value);
}

void vesc_host_make_var_port(motor_config_var_port_t *out, flash_emul_t *emul) {
    if (out == (void *)0) {
        return;
    }

    *out = (motor_config_var_port_t){
        .read = var_read,
        .write = var_write,
        .self = emul,
    };
}

static edge_status_t ops_terminal_cmd(void *self, const char *cmd) {
    vesc_host_ops_ctx_t *ctx = (vesc_host_ops_ctx_t *)self;
    if (ctx->term == (void *)0) {
        return EDGE_ENOTSUP;
    }
    return vesc_terminal_execute(ctx->term, cmd);
}

static edge_status_t ops_forward_can(void *self, uint8_t target_id, const uint8_t *data,
                                     size_t len) {
    vesc_host_ops_ctx_t *ctx = (vesc_host_ops_ctx_t *)self;
    if (ctx->can == (void *)0) {
        return EDGE_ENOTSUP;
    }
    /* `send` is zero, as the reference passes it on the forward path. */
    return vesc_can_send_buffer(ctx->can, target_id, data, len, 0u);
}

void vesc_host_make_ops_port(vesc_comm_ops_port_t *out, vesc_host_ops_ctx_t *ctx) {
    if (out == (void *)0) {
        return;
    }

    *out = (vesc_comm_ops_port_t){
        .terminal_cmd = ops_terminal_cmd,
        .forward_can = ops_forward_can,
        .self = ctx,
    };
}

void vesc_host_make_motor_provider_port(vesc_motor_provider_port_t *out, foc_core_t *foc) {
    if (!out || !foc) {
        return;
    }
    *out = (vesc_motor_provider_port_t){
        .get_values = motor_get_values,
        .set_duty = motor_set_duty,
        .set_current = motor_set_current,
        .set_current_rel = motor_set_current_rel,
        .set_handbrake = motor_set_handbrake,
        .get_setup_values = motor_get_setup_values,
        .set_current_brake = motor_set_current_brake,
        .set_rpm = motor_set_rpm,
        .set_pos = motor_set_pos,
        .get_stats = motor_get_stats,
        .reset_stats = motor_reset_stats,
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

/* Decoded app inputs (COMM_GET_DECODED_PPM / _ADC). The reference reads these from
 * app_ppm and app_adc directly; here the adapter reads the same apps through their
 * public getters. */
static edge_status_t app_get_decoded_ppm(void *self, float *level, float *pulse_us) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->ppm == (void *)0) {
        return EDGE_EINVAL;
    }
    if (level != (void *)0) {
        *level = ppm_get_output(s->ppm);
    }
    if (pulse_us != (void *)0) {
        *pulse_us = ppm_get_last_pulse_us(s->ppm);
    }
    return EDGE_OK;
}

static edge_status_t app_get_decoded_adc(void *self, float *level, float *voltage, float *level2,
                                         float *voltage2) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->adc == (void *)0) {
        return EDGE_EINVAL;
    }
    if (level != (void *)0) {
        *level = adc_input_get_throttle(s->adc);
    }
    if (voltage != (void *)0) {
        *voltage = adc_input_get_throttle_v(s->adc);
    }
    if (level2 != (void *)0) {
        *level2 = adc_input_get_brake(s->adc);
    }
    if (voltage2 != (void *)0) {
        *voltage2 = adc_input_get_brake_v(s->adc);
    }
    return EDGE_OK;
}

void vesc_host_make_app_status_port(vesc_app_status_port_t *out, vesc_host_glue_state_t *state) {
    if (!out || !state) {
        return;
    }
    *out = (vesc_app_status_port_t){
        .get_decoded_ppm = app_get_decoded_ppm,
        .get_decoded_adc = app_get_decoded_adc,
        .self = state,
    };
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

/* Motor ID Measure Port Adaptors: the reference's measurement procedures run against the FOC
 * aggregate, so each callback is one thing those procedures do to the motor. */
static edge_status_t id_set_phase_override(void *self, float angle_rad, bool enable) {
    foc_core_set_phase_override((foc_core_t *)self, angle_rad, enable);
    return EDGE_OK;
}

/* The reference holds id at zero and ramps iq (mcpwm_foc.c:1805-1807). */
static edge_status_t id_set_current(void *self, float iq) {
    return foc_core_set_current((foc_core_t *)self, iq, 0.0f);
}

static edge_status_t id_reset_samples(void *self) {
    foc_core_reset_detect_samples((foc_core_t *)self);
    return EDGE_OK;
}

static edge_status_t id_read_samples(void *self, float *i_sum, float *v_sum, uint32_t *count) {
    foc_core_read_detect_samples((foc_core_t *)self, i_sum, v_sum, count);
    return EDGE_OK;
}

static uint32_t id_get_fault(void *self) {
    /* The procedures compare this against "no fault", which is zero, so the FOC's fault bits
     * serve as they are rather than being translated into the reference's fault_code enum. */
    return foc_core_get_faults((foc_core_t *)self);
}

static edge_status_t id_stop(void *self) {
    return foc_core_stop((foc_core_t *)self);
}

void vesc_host_make_motor_id_measure_port(motor_id_measure_port_t *out, foc_core_t *foc) {
    if (out == (void *)0 || foc == (void *)0) {
        return;
    }
    *out = (motor_id_measure_port_t){
        .self = foc,
        .set_phase_override = id_set_phase_override,
        .set_current = id_set_current,
        .reset_samples = id_reset_samples,
        .read_samples = id_read_samples,
        .get_fault = id_get_fault,
        .stop = id_stop,
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
