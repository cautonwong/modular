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

/*
 * COMM_DETECT_MOTOR_FLUX_LINKAGE_OPENLOOP. The reference blocks its command thread on the procedure
 * while the control loop keeps running in its own interrupt; here the two are advanced together,
 * the control loop at the rate the product runs it at and the procedure one millisecond at a time.
 * Twenty control cycles per millisecond is the host product's own fifty-microsecond period.
 */
static edge_status_t ops_detect_flux_linkage_openloop(void *self, float current_a, float duty,
                                                      float erpm_per_sec, float resistance_ohm,
                                                      float inductance_h,
                                                      vesc_detect_flux_result_t *result) {
    vesc_host_ops_ctx_t *ctx = (vesc_host_ops_ctx_t *)self;
    if (ctx == (void *)0 || ctx->glue == (void *)0 || ctx->motor_id == (void *)0 ||
        ctx->glue->foc == (void *)0 || result == (void *)0) {
        return EDGE_EINVAL;
    }

    foc_core_t *foc = ctx->glue->foc;
    motor_id_app_t *app = ctx->motor_id;
    const float foc_dt = 0.000050f;
    const uint32_t cycles_per_ms = 20u;

    /* A resistance or inductance of zero means "take the configuration's", which is the one the
     * composition root built the aggregate from. */
    if (motor_id_measure_flux_linkage_openloop(app, current_a, duty, erpm_per_sec, resistance_ohm,
                                               inductance_h, foc->config.r_ohm, foc->config.l_henry,
                                               foc->config.duty_max) != EDGE_OK) {
        return EDGE_EINVAL;
    }

    /* Bounded by the procedure's own longest phase - fifteen seconds of spin-up plus the rest -
     * with room to spare, so a measurement that never finishes cannot hang the caller. */
    for (uint32_t ms = 0u; ms < 40000u; ++ms) {
        if (app->state == MOTOR_ID_STATE_COMPLETE || app->state == MOTOR_ID_STATE_FAILED) {
            break;
        }
        for (uint32_t cycle = 0u; cycle < cycles_per_ms; ++cycle) {
            (void)foc_core_fast_loop(foc, foc_dt);
            foc_virtual_motor_step(&ctx->glue->vmotor, foc->v_alpha, foc->v_beta, 0.0f, foc_dt,
                                   0.0f);
        }
        (void)motor_id_step(app, 0.001f);
    }

    const motor_id_result_t *measured = motor_id_get_result(app);
    result->linkage_wb = measured->flux_linkage_wb;
    result->linkage_undriven_wb = measured->linkage_undriven_wb;
    result->undriven_samples = measured->undriven_samples;
    result->valid = measured->valid;
    return EDGE_OK;
}

/*
 * COMM_DETECT_MOTOR_FLUX_LINKAGE: the sensored measurement, driven the same way as the open-loop
 * one
 * - the procedure and the plant advance together. The procedure reports success as a bool, which
 * here is the state it ends in: anything but complete is a failure, and the reply is then a zero.
 */
static edge_status_t ops_detect_flux_linkage(void *self, float current_a, float min_rpm, float duty,
                                             float resistance_ohm, float *linkage_wb) {
    vesc_host_ops_ctx_t *ctx = (vesc_host_ops_ctx_t *)self;
    if (ctx == (void *)0 || ctx->glue == (void *)0 || ctx->motor_id == (void *)0 ||
        ctx->glue->foc == (void *)0 || linkage_wb == (void *)0) {
        return EDGE_EINVAL;
    }

    foc_core_t *foc = ctx->glue->foc;
    motor_id_app_t *app = ctx->motor_id;
    const float foc_dt = 0.000050f;
    const uint32_t cycles_per_ms = 20u;

    if (motor_id_measure_flux_linkage_sensored(app, current_a, duty, min_rpm, resistance_ohm,
                                               foc->config.r_ohm) != EDGE_OK) {
        return EDGE_EINVAL;
    }

    /* Bounded by the procedure's own worst case - the four attempts at up to six seconds each plus
     * the averaging - with room to spare, so a measurement that never finishes cannot hang. */
    for (uint32_t ms = 0u; ms < 60000u; ++ms) {
        if (app->state == MOTOR_ID_STATE_COMPLETE || app->state == MOTOR_ID_STATE_FAILED) {
            break;
        }
        for (uint32_t cycle = 0u; cycle < cycles_per_ms; ++cycle) {
            (void)foc_core_fast_loop(foc, foc_dt);
            foc_virtual_motor_step(&ctx->glue->vmotor, foc->v_alpha, foc->v_beta, 0.0f, foc_dt,
                                   0.0f);
        }
        (void)motor_id_step(app, 0.001f);
    }

    if (app->state != MOTOR_ID_STATE_COMPLETE) {
        return EDGE_EIO;
    }
    *linkage_wb = motor_id_get_result(app)->flux_linkage_wb;
    return EDGE_OK;
}

/*
 * COMM_DETECT_MOTOR_R_L. The reference stages a copy of its configuration with the motor type set
 * to FOC and the switching frequency at ten kilohertz - a lower frequency means less dead-time
 * distortion and more current available to measure inductance with - runs the composed sequence,
 * and puts the configuration back before it replies (:2128-2140). The staging is the glue's here
 * rather than the command layer's, because the command layer reaches the motor only through this
 * port.
 *
 * The plant is stepped with mod_alpha_raw, as the inductance measurement's own closed loop is: the
 * excitation is what it measures itself with.
 */
static edge_status_t ops_detect_r_l(void *self, vesc_detect_r_l_result_t *result) {
    vesc_host_ops_ctx_t *ctx = (vesc_host_ops_ctx_t *)self;
    if (ctx == (void *)0 || ctx->glue == (void *)0 || ctx->motor_id == (void *)0 ||
        ctx->glue->foc == (void *)0 || result == (void *)0) {
        return EDGE_EINVAL;
    }

    foc_core_t *foc = ctx->glue->foc;
    motor_id_app_t *app = ctx->motor_id;
    /* The switching frequency is the ten kilohertz above, so the interrupt runs at twice it - the
     * host product's own fifty-microsecond period. */
    const float foc_dt = 0.000050f;
    const uint32_t cycles_per_ms = 20u;

    const foc_config_t saved = foc->config;
    foc->config.f_zv = 10000.0f;
    memset(result, 0, sizeof(*result));

    edge_status_t status = motor_id_measure_r_l(app, foc->config.current_max_a);
    if (status == EDGE_OK) {
        /* Bounded by the sequence's own longest run, so a measurement that never finishes cannot
         * hang the caller. */
        for (uint32_t ms = 0u; ms < 40000u; ++ms) {
            if (app->state == MOTOR_ID_STATE_COMPLETE || app->state == MOTOR_ID_STATE_FAILED) {
                break;
            }
            for (uint32_t cycle = 0u; cycle < cycles_per_ms; ++cycle) {
                (void)foc_core_fast_loop(foc, foc_dt);
                foc_virtual_motor_step(&ctx->glue->vmotor, foc->mod_alpha_raw, foc->mod_beta_raw,
                                       0.0f, foc_dt, 0.0f);
            }
            (void)motor_id_step(app, 0.001f);
        }

        const motor_id_result_t *measured = motor_id_get_result(app);
        result->r_ohm = measured->r_ohm;
        result->l_uh = measured->ind_uh;
        result->ld_lq_diff_uh = measured->ld_lq_diff_uh;
        result->valid = measured->valid;
    }

    foc->config = saved;
    return status;
}

void vesc_host_make_ops_port(vesc_comm_ops_port_t *out, vesc_host_ops_ctx_t *ctx) {
    if (out == (void *)0) {
        return;
    }

    *out = (vesc_comm_ops_port_t){
        .terminal_cmd = ops_terminal_cmd,
        .forward_can = ops_forward_can,
        .detect_flux_linkage_openloop = ops_detect_flux_linkage_openloop,
        .detect_flux_linkage = ops_detect_flux_linkage,
        .detect_r_l = ops_detect_r_l,
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
 * aggregate, so each callback is one thing those procedures do to the motor. The port's own state
 * is this product's, and it reaches the aggregate through it - the flux-linkage procedure needs
 * somewhere to keep the configuration it temporarily replaces. */
static foc_core_t *id_foc(void *self) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    return (s != (void *)0) ? s->foc : (void *)0;
}

static edge_status_t id_set_phase_override(void *self, float angle_rad, bool enable) {
    foc_core_set_phase_override(id_foc(self), angle_rad, enable);
    return EDGE_OK;
}

/* The reference holds id at zero and ramps iq (mcpwm_foc.c:1805-1807). */
static edge_status_t id_set_current(void *self, float iq) {
    return foc_core_set_current(id_foc(self), iq, 0.0f);
}

static edge_status_t id_reset_samples(void *self) {
    foc_core_reset_detect_samples(id_foc(self));
    return EDGE_OK;
}

static edge_status_t id_read_samples(void *self, float *i_sum, float *v_sum, uint32_t *count) {
    foc_core_read_detect_samples(id_foc(self), i_sum, v_sum, count);
    return EDGE_OK;
}

static uint32_t id_get_fault(void *self) {
    /* The procedures compare this against "no fault", which is zero, so the FOC's fault bits
     * serve as they are rather than being translated into the reference's fault_code enum. */
    return foc_core_get_faults(id_foc(self));
}

static edge_status_t id_stop(void *self) {
    return foc_core_stop(id_foc(self));
}

/*
 * The temporary configuration a measurement runs in. This product's live configuration is the FOC
 * aggregate's own - the composition root builds it once from motor_config - so entering saves the
 * three fields the procedure is about to change and leaving puts them back. The reference edits
 * the same live configuration (conf_general.c:1007-1016): sensorless, the current gains computed
 * from the supplied resistance and inductance, and cross-coupling decoupling off, which this port
 * does not carry as a configuration field.
 */
static edge_status_t id_enter_measurement_config(void *self, float current_kp, float current_ki) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0) {
        return EDGE_EINVAL;
    }
    s->saved_foc_config = s->foc->config;
    s->foc_config_saved = true;
    s->foc->config.sensorless_mode = true;
    s->foc->config.current_kp = current_kp;
    s->foc->config.current_ki = current_ki;
    return EDGE_OK;
}

static edge_status_t id_leave_measurement_config(void *self) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0 || !s->foc_config_saved) {
        return EDGE_EINVAL;
    }
    s->foc->config = s->saved_foc_config;
    s->foc_config_saved = false;
    return EDGE_OK;
}

static edge_status_t id_set_openloop_current(void *self, float current_a, float rpm) {
    return foc_core_set_openloop_current(id_foc(self), current_a, rpm);
}

static edge_status_t id_read_vdq(void *self, float *v_d, float *v_q) {
    foc_core_t *foc = id_foc(self);
    if (foc == (void *)0 || v_d == (void *)0 || v_q == (void *)0) {
        return EDGE_EINVAL;
    }
    *v_d = foc->v_d;
    *v_q = foc->v_q;
    return EDGE_OK;
}

static edge_status_t id_read_idq(void *self, float *i_d, float *i_q) {
    foc_core_t *foc = id_foc(self);
    if (foc == (void *)0 || i_d == (void *)0 || i_q == (void *)0) {
        return EDGE_EINVAL;
    }
    *i_d = foc->last_id;
    *i_q = foc->last_iq;
    return EDGE_OK;
}

static edge_status_t id_read_duty(void *self, float *duty_now) {
    foc_core_t *foc = id_foc(self);
    if (foc == (void *)0 || duty_now == (void *)0) {
        return EDGE_EINVAL;
    }
    *duty_now = foc->duty_now;
    return EDGE_OK;
}

/*
 * The electrical angular speed the undriven measurement divides by (conf_general.c:1216). The FOC
 * reports mechanical rpm, so the pole pairs are applied first - the same conversion the speed loop
 * does - and that is what RPM2RADPS_f takes.
 */
static edge_status_t id_read_speed_rad_s(void *self, float *rad_s) {
    foc_core_t *foc = id_foc(self);
    if (foc == (void *)0 || rad_s == (void *)0) {
        return EDGE_EINVAL;
    }
    const float erpm = foc->last_rpm * ((float)foc->config.si_motor_poles / 2.0f);
    *rad_s = erpm * (float)(2.0 * 3.14159265358979323846 / 60.0);
    return EDGE_OK;
}

/*
 * The sensored flux-linkage procedure's own callbacks. Three of them read what the aggregate
 * already publishes, one asks it to release the motor and one asks whether it has; the last saves
 * the configuration and commutes from the rotor sensor instead of sensorless, which is the one
 * thing that differs between the two flux-linkage procedures.
 */
static edge_status_t id_read_vbus(void *self, float *v_bus) {
    foc_core_t *foc = id_foc(self);
    if (foc == (void *)0 || v_bus == (void *)0) {
        return EDGE_EINVAL;
    }
    *v_bus = foc->last_v_bus;
    return EDGE_OK;
}

/* The reference reads mc_interface_get_rpm(), which is mechanical, and multiplies it by
 * RPM2RADPS_f itself; this hands over the same quantity. */
static edge_status_t id_read_rpm(void *self, float *rpm) {
    foc_core_t *foc = id_foc(self);
    if (foc == (void *)0 || rpm == (void *)0) {
        return EDGE_EINVAL;
    }
    *rpm = foc->last_rpm;
    return EDGE_OK;
}

static edge_status_t id_release_motor(void *self) {
    return foc_core_release_motor(id_foc(self));
}

static edge_status_t id_is_running(void *self, bool *running) {
    foc_core_t *foc = id_foc(self);
    if (foc == (void *)0 || running == (void *)0) {
        return EDGE_EINVAL;
    }
    const foc_state_t state = foc->state;
    *running = state == FOC_STATE_RUNNING_CURRENT || state == FOC_STATE_RUNNING_DUTY ||
               state == FOC_STATE_RUNNING_RPM || state == FOC_STATE_RUNNING_POS ||
               state == FOC_STATE_HANDBRAKE || state == FOC_STATE_RUNNING_OPENLOOP;
    return EDGE_OK;
}

/*
 * The per-attempt start-up limits, which this port has nowhere to put: they tune the reference's
 * sensorless start (sl_min_erpm, sl_cycle_int_limit and a delayed commutation mode), and this
 * controller does not use any of the three. The callback answers that it was given them and changes
 * nothing, which is recorded in the conformance view - the four attempts then differ by the release
 * and the re-drive rather than by a different tune.
 */
static edge_status_t id_set_startup_limits(void *self, float sl_min_erpm, float sl_cycle_limit,
                                           bool delay_comm_mode) {
    (void)self;
    (void)sl_min_erpm;
    (void)sl_cycle_limit;
    (void)delay_comm_mode;
    return EDGE_OK;
}

static edge_status_t id_enter_sensored_measurement_config(void *self) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0) {
        return EDGE_EINVAL;
    }
    s->saved_foc_config = s->foc->config;
    s->foc_config_saved = true;
    /* This board has no hall driver, but the host simulation has a rotor sensor, so the procedure
     * can run here the way it runs on a board that does. The reference picks a specific sensored
     * mode; this port's configuration says only whether the angle comes from the sensor. */
    s->foc->config.sensorless_mode = false;
    return EDGE_OK;
}

/*
 * Inductance (mcpwm_foc_measure_inductance, :1909-2070). Its temporary configuration is a
 * save-and-restore pair like the flux procedure's: the HFI sensor mode with the six-vector
 * ambiguity mode, the three excitation voltages computed from the caller's duty against the bus,
 * the speed override, the sampling mode that puts the interrupt in the first zero vector, the
 * thirty-two sample table, and the switching-frequency clamp that this measurement is the only
 * caller of (:1918-1935). The reference stops the PWM before installing it, which is where the
 * bus voltage it needs comes from - the filtered input voltage, which this glue keeps as the last
 * one the control loop read.
 */
static edge_status_t id_enter_inductance_config(void *self, float duty) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0) {
        return EDGE_EINVAL;
    }
    s->saved_foc_config = s->foc->config;
    s->foc_config_saved = true;

    const float voltage = duty * s->foc->last_v_bus * (2.0f / 3.0f) * 0.8660254f;
    foc_config_t *cfg = &s->foc->config;
    cfg->sensor_mode = FOC_ANGLE_SOURCE_HFI;
    cfg->sensorless_mode = true;
    cfg->hfi_amb_mode_six_vector = true;
    cfg->hfi_control_sample_mode_v0_v7 = false;
    cfg->hfi_voltage_start = voltage;
    cfg->hfi_voltage_run = voltage;
    cfg->hfi_voltage_max = voltage;
    cfg->sl_erpm_hfi = 20000.0f;
    cfg->hfi_samples = 2u; /* HFI_SAMPLES_32 */
    if (cfg->f_zv > 30000.0f) {
        cfg->f_zv = 30000.0f;
    }
    foc_hfi_configure(&s->foc->hfi, cfg->hfi_samples);
    (void)foc_core_stop(s->foc);
    return EDGE_OK;
}

static edge_status_t id_leave_inductance_config(void *self) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0 || !s->foc_config_saved) {
        return EDGE_EINVAL;
    }
    s->foc->config = s->saved_foc_config;
    s->foc_config_saved = false;
    foc_hfi_configure(&s->foc->hfi, s->foc->config.hfi_samples);
    return EDGE_OK;
}

static edge_status_t id_set_duty(void *self, float duty) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0) {
        return EDGE_EINVAL;
    }
    return foc_core_set_duty(s->foc, duty);
}

static edge_status_t id_is_hfi_ready(void *self, bool *ready) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0 || ready == (void *)0) {
        return EDGE_EINVAL;
    }
    *ready = s->foc->hfi.ready;
    return EDGE_OK;
}

static edge_status_t id_read_hfi_bins(void *self, float *offset, float *real_bin2, float *imag_bin2,
                                      float *current_mean) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0) {
        return EDGE_EINVAL;
    }
    foc_core_read_hfi_bins(s->foc, offset, real_bin2, imag_bin2, current_mean);
    return EDGE_OK;
}

/* mcpwm_foc_measure_res_ind's own gains (:2322-2326): tiny ones, because that scan reads the
 * voltage the resistance drops rather than a controller's output. The reference restores them at
 * its single exit, which is why this is a pair. */
static edge_status_t id_enter_res_ind_gains(void *self) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0) {
        return EDGE_EINVAL;
    }
    s->saved_current_kp = s->foc->config.current_kp;
    s->saved_current_ki = s->foc->config.current_ki;
    s->foc->config.current_kp = 0.001f;
    s->foc->config.current_ki = 1.0f;
    s->res_ind_gains_saved = true;
    return EDGE_OK;
}

static edge_status_t id_leave_res_ind_gains(void *self) {
    vesc_host_glue_state_t *s = (vesc_host_glue_state_t *)self;
    if (s == (void *)0 || s->foc == (void *)0 || !s->res_ind_gains_saved) {
        return EDGE_EINVAL;
    }
    s->foc->config.current_kp = s->saved_current_kp;
    s->foc->config.current_ki = s->saved_current_ki;
    s->res_ind_gains_saved = false;
    return EDGE_OK;
}

void vesc_host_make_motor_id_measure_port(motor_id_measure_port_t *out,
                                          vesc_host_glue_state_t *state) {
    if (out == (void *)0 || state == (void *)0) {
        return;
    }
    *out = (motor_id_measure_port_t){
        .self = state,
        .set_phase_override = id_set_phase_override,
        .set_current = id_set_current,
        .reset_samples = id_reset_samples,
        .read_samples = id_read_samples,
        .get_fault = id_get_fault,
        .stop = id_stop,
        .enter_measurement_config = id_enter_measurement_config,
        .leave_measurement_config = id_leave_measurement_config,
        .set_openloop_current = id_set_openloop_current,
        .read_vdq = id_read_vdq,
        .read_idq = id_read_idq,
        .read_duty = id_read_duty,
        .read_speed_rad_s = id_read_speed_rad_s,
        .read_vbus = id_read_vbus,
        .read_rpm = id_read_rpm,
        .release_motor = id_release_motor,
        .is_running = id_is_running,
        .set_startup_limits = id_set_startup_limits,
        .enter_sensored_measurement_config = id_enter_sensored_measurement_config,
        .enter_inductance_config = id_enter_inductance_config,
        .leave_inductance_config = id_leave_inductance_config,
        .set_duty = id_set_duty,
        .is_hfi_ready = id_is_hfi_ready,
        .read_hfi_bins = id_read_hfi_bins,
        .enter_res_ind_gains = id_enter_res_ind_gains,
        .leave_res_ind_gains = id_leave_res_ind_gains,
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
