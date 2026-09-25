#include "foc_core/foc_core.h"
#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"
#include <math.h>
#include <string.h>

static edge_status_t foc_core_poll(edge_module_t *module) {
    foc_core_t *self = (foc_core_t *)edge_module_data(module);
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    self->step_count++;

    /*
     * Average sampler. The reference keeps this separate from the control path:
     * mc_interface.c accumulates m_motor_{id,iq,vd,vq,current}_sum from the last
     * FOC values on a periodic tick, so the sums advance even when the control
     * path did not run this iteration, and mc_interface_read_reset_avg_* divides
     * by the iteration count and zeroes. Same split here - the fast loop stores,
     * the periodic poll accumulates.
     */
    self->avg_id_sum += self->last_id;
    self->avg_iq_sum += self->last_iq;
    self->avg_vd_sum += self->v_d;
    self->avg_vq_sum += self->v_q;
    self->avg_motor_current_sum += self->i_abs_filter;
    self->avg_id_iterations += 1.0f;
    self->avg_iq_iterations += 1.0f;
    self->avg_vd_iterations += 1.0f;
    self->avg_vq_iterations += 1.0f;
    self->avg_motor_current_iterations += 1.0f;

    /*
     * Statistics sampler. Reference: update_stats() in mc_interface.c, driven by a
     * dedicated thread rather than the control path. Power is input voltage times
     * the absolute input current (the reference filters that voltage; this port has
     * no filtered bus voltage), the current term is the RAW motor current
     * magnitude, and the temperature term the FET temperature. No motor NTC is
     * wired, so the motor-temperature statistics stay at their -300 seed.
     */
    /*
     * Vehicle speed: reference mc_interface_get_speed(). The reference divides
     * ERPM by (si_motor_poles / 2) to get mechanical rpm and then scales by the
     * wheel diameter and gear ratio; the rotor port here already reports
     * mechanical rpm, so only the scaling part is reproduced.
     */
    self->speed_m_s = (self->last_rpm / 60.0f) * self->config.si_wheel_diameter * (float)M_PI /
                      self->config.si_gear_ratio;

    float stat_power = self->last_v_bus * fabsf(self->i_bus);
    const float stat_temp_motor = 0.0f;

    self->stat_power_sum += stat_power;
    self->stat_current_sum += self->i_abs;
    self->stat_temp_mos_sum += self->fet_temp_c;
    self->stat_temp_motor_sum += stat_temp_motor;
    self->stat_speed_sum += fabsf(self->speed_m_s);
    self->stat_samples += 1.0f;

    if (fabsf(self->speed_m_s) > self->stat_max_speed) {
        self->stat_max_speed = fabsf(self->speed_m_s);
    }
    if (stat_power > self->stat_max_power) {
        self->stat_max_power = stat_power;
    }
    if (self->i_abs > self->stat_max_current) {
        self->stat_max_current = self->i_abs;
    }
    if (self->fet_temp_c > self->stat_max_temp_mos) {
        self->stat_max_temp_mos = self->fet_temp_c;
    }
    if (stat_temp_motor > self->stat_max_temp_motor) {
        self->stat_max_temp_motor = stat_temp_motor;
    }

    /* Background Thermal Protection Check */
    if (self->fet_temp_c > self->config.temp_fet_max_c && self->config.temp_fet_max_c > 1.0f) {
        self->faults |= FOC_FAULT_OVER_TEMP;
        self->state = FOC_STATE_FAULT;
        if (self->inverter != (void *)0 && self->inverter->set_phase_state != (void *)0) {
            (void)self->inverter->set_phase_state(self->inverter->self, false);
        }
    }

    return EDGE_OK;
}

static edge_status_t foc_core_on_event(edge_module_t *module, const edge_event_t *event) {
    foc_core_t *self = (foc_core_t *)edge_module_data(module);
    if (self == (void *)0 || event == (void *)0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t foc_core_power_off(edge_module_t *module) {
    foc_core_t *self = (foc_core_t *)edge_module_data(module);
    if (self != (void *)0) {
        (void)foc_core_stop(self);
    }
    return EDGE_OK;
}

void foc_core_construct(foc_core_t *self, uint32_t module_id, uint32_t priority,
                        const foc_config_t *config, const foc_inverter_port_t *inverter,
                        const foc_current_port_t *current_sensor,
                        const foc_rotor_port_t *rotor_sensor) {
    if (self == (void *)0) {
        return;
    }

    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = foc_core_poll,
        .on_event = foc_core_on_event,
        .power_off = foc_core_power_off,
        .private_data = self,
    };

    self->inverter = inverter;
    self->current_sensor = current_sensor;
    self->rotor_sensor = rotor_sensor;

    if (config != (void *)0) {
        self->config = *config;
    } else {
        self->config.r_ohm = 0.05f;
        self->config.l_henry = 0.00005f;
        self->config.lambda_wb = 0.005f;
        self->config.si_motor_poles = 14u;
        self->config.si_gear_ratio = 3.0f;
        self->config.si_wheel_diameter = 0.083f;
        self->config.current_max_a = 50.0f;
        self->config.current_min_a = -50.0f;
        self->config.duty_max = 0.95f;
        self->config.current_kp = 0.1f;
        self->config.current_ki = 100.0f;
        self->config.vbus_ov_threshold = 60.0f;
        self->config.vbus_uv_threshold = 8.0f;
        self->config.temp_fet_max_c = 100.0f;
        self->config.current_filter_const = 0.1f;
        self->config.sensorless_mode = false;
        self->config.observer_gamma = 9.0e5f;
        self->config.observer_type = FOC_OBSERVER_ORTEGA_ORIGINAL;
        self->config.sat_comp_mode = 0u; /* SAT_COMP_DISABLED */
        self->config.sat_comp = 0.0f;
        self->config.ld_lq_diff = 0.0f;
        /* Reference defaults (mcconf_default.h): 0.004 / 0.004 / 0.0001 / 0.2,
         * braking allowed, 25000 ERPM/s ramp, min_erpm 0. */
        self->config.speed_pid = (foc_speed_pid_params_t){
            .kp = 0.004f,
            .ki = 0.004f,
            .kd = 0.0001f,
            .kd_filter = 0.2f,
            .ramp_erpms_s = 25000.0f,
            .min_erpm = 0.0f,
            .openloop_rpm = 700.0f,
            .l_min_erpm = -100000.0f,
            .l_max_erpm = 100000.0f,
            .lo_current_max = 60.0f,
            .current_max_scale = 1.0f,
            .allow_braking = true,
            .invert_direction = false,
        };
        self->config.pll_kp = 2000.0f;
        self->config.pll_ki = 30000.0f;
    }

    self->state = FOC_STATE_UNINITIALIZED;
    self->faults = FOC_FAULT_NONE;

    self->target_id = 0.0f;
    self->target_iq = 0.0f;
    self->target_duty = 0.0f;
    self->target_rpm = 0.0f;

    self->id_integral = 0.0f;
    self->iq_integral = 0.0f;
    self->v_d = 0.0f;
    self->v_q = 0.0f;
    self->duty_now = 0.0f;
    self->duty_abs_filtered = 0.0f;
    self->mod_q_filter = 0.0f;
    self->i_fw_set = 0.0f;
    self->v_alpha = 0.0f;
    self->v_beta = 0.0f;
    self->duty_a = 0.5f;
    self->duty_b = 0.5f;
    self->duty_c = 0.5f;
    self->svm_sector = 1u;

    foc_observer_init(&self->observer, self->config.lambda_wb);
    self->pll.phase = 0.0f;
    self->pll.speed = 0.0f;

    self->last_v_bus = 0.0f;
    self->last_ia = 0.0f;
    self->last_ib = 0.0f;
    self->last_ic = 0.0f;
    self->last_id = 0.0f;
    self->last_iq = 0.0f;
    self->last_angle_rad = 0.0f;
    self->last_rpm = 0.0f;
    self->fet_temp_c = 25.0f;

    self->id_filter = 0.0f;
    self->iq_filter = 0.0f;
    self->i_abs_filter = 0.0f;
    self->i_bus = 0.0f;
    self->i_abs = 0.0f;
    self->speed_m_s = 0.0f;
    self->tacho_step_last = 0;
    self->tachometer = 0;
    self->tachometer_abs = 0;
    self->stat_speed_sum = 0.0f;
    self->stat_max_speed = 0.0f;
    self->stat_speed_sum = 0.0f;
    self->stat_max_speed = 0.0f;
    self->stat_samples = 0.0f;
    self->stat_power_sum = 0.0f;
    self->stat_max_power = 0.0f;
    self->stat_current_sum = 0.0f;
    self->stat_max_current = 0.0f;
    self->stat_temp_mos_sum = 0.0f;
    self->stat_temp_motor_sum = 0.0f;
    /* The reference's stat_reset() seeds the temperature maxima below any real
     * reading, so the first sample becomes the maximum. */
    self->stat_max_temp_mos = -300.0f;
    self->stat_max_temp_motor = -300.0f;
    self->amp_seconds = 0.0f;
    self->amp_seconds_charged = 0.0f;
    self->watt_seconds = 0.0f;
    self->watt_seconds_charged = 0.0f;

    self->fast_loop_count = 0u;
    self->step_count = 0u;

    /*
     * Averaging accumulators must start at zero. Leaving them to the caller's
     * stack was not caught by a single green test run - it showed up only as
     * flaky averages, because the first read divided garbage by garbage.
     */
    self->avg_id_sum = 0.0f;
    self->avg_iq_sum = 0.0f;
    self->avg_vd_sum = 0.0f;
    self->avg_vq_sum = 0.0f;
    self->avg_motor_current_sum = 0.0f;
    self->avg_input_current_sum = 0.0f;
    self->avg_id_iterations = 0.0f;
    self->avg_iq_iterations = 0.0f;
    self->avg_vd_iterations = 0.0f;
    self->avg_vq_iterations = 0.0f;
    self->avg_motor_current_iterations = 0.0f;
    self->avg_input_current_iterations = 0.0f;
}

edge_status_t foc_core_init(foc_core_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* Validate ports */
    if (self->inverter == (void *)0 || self->inverter->set_duty == (void *)0 ||
        self->inverter->set_phase_state == (void *)0) {
        return EDGE_EINVAL;
    }

    if (self->current_sensor == (void *)0 || self->current_sensor->read_currents == (void *)0 ||
        self->current_sensor->read_vbus == (void *)0) {
        return EDGE_EINVAL;
    }

    if (!self->config.sensorless_mode) {
        if (self->rotor_sensor == (void *)0 || self->rotor_sensor->read_angle == (void *)0) {
            return EDGE_EINVAL;
        }
    }

    /* Validate configuration parameters */
    if (self->config.r_ohm <= 0.0f || self->config.l_henry <= 0.0f ||
        self->config.lambda_wb <= 0.0f || self->config.si_motor_poles < 2u ||
        self->config.current_max_a <= 0.0f) {
        self->faults |= FOC_FAULT_INVALID_CONFIG;
        self->state = FOC_STATE_FAULT;
        return EDGE_EINVAL;
    }

    foc_observer_init(&self->observer, self->config.lambda_wb);

    self->id_integral = 0.0f;
    self->iq_integral = 0.0f;
    self->faults = FOC_FAULT_NONE;
    self->state = FOC_STATE_IDLE;

    (void)self->inverter->set_phase_state(self->inverter->self, false);

    return EDGE_OK;
}

edge_status_t foc_core_deinit(foc_core_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    (void)foc_core_stop(self);
    self->state = FOC_STATE_UNINITIALIZED;
    return EDGE_OK;
}

edge_module_t *foc_core_module(foc_core_t *self) {
    return (self != (void *)0) ? &self->module : (void *)0;
}

edge_status_t foc_core_fast_loop(foc_core_t *self, float dt) {
    if (self == (void *)0 || dt <= 0.0f) {
        return EDGE_EINVAL;
    }

    self->fast_loop_count++;

    /* 1. Read sensors via consumer ports */
    float ia = 0.0f, ib = 0.0f, ic = 0.0f;
    edge_status_t st =
        self->current_sensor->read_currents(self->current_sensor->self, &ia, &ib, &ic);
    if (st != EDGE_OK) {
        return st;
    }

    float v_bus = 0.0f;
    st = self->current_sensor->read_vbus(self->current_sensor->self, &v_bus);
    if (st != EDGE_OK) {
        return st;
    }

    self->last_ia = ia;
    self->last_ib = ib;
    self->last_ic = ic;
    self->last_v_bus = v_bus;

    /* 2. Voltage Safety Invariant Checks */
    if (v_bus > self->config.vbus_ov_threshold && self->config.vbus_ov_threshold > 0.0f) {
        self->faults |= FOC_FAULT_OVER_VOLTAGE;
        self->state = FOC_STATE_FAULT;
        (void)self->inverter->set_phase_state(self->inverter->self, false);
        return EDGE_EBUSY;
    }

    if (v_bus < self->config.vbus_uv_threshold && self->state >= FOC_STATE_RUNNING_CURRENT) {
        self->faults |= FOC_FAULT_UNDER_VOLTAGE;
        self->state = FOC_STATE_FAULT;
        (void)self->inverter->set_phase_state(self->inverter->self, false);
        return EDGE_EBUSY;
    }

    /* 3. Clarke Transform */
    float i_alpha = 0.0f, i_beta = 0.0f;
    foc_clarke_transform(ia, ib, ic, &i_alpha, &i_beta);

    /* 4. Rotor Angle Feedback */
    float angle_rad = 0.0f;
    float rpm = 0.0f;
    if (self->config.sensorless_mode) {
        /* Compensation first: the reference applies it inside the observer, this port
         * applies it here and keeps the observer's parameters explicit. It reads the
         * previous pass's currents, as the reference does. Temperature compensation is
         * not carried (no motor-temperature source), so it passes false. */
        float r_eff = self->config.r_ohm;
        float l_eff = self->config.l_henry;
        float lambda_eff = self->config.lambda_wb;
        foc_observer_adjust_params(
            self->config.r_ohm, self->config.l_henry, self->config.lambda_wb,
            self->config.ld_lq_diff, self->last_id, self->last_iq, self->i_abs_filter,
            self->config.current_max_a, self->observer.lambda_est, self->config.sat_comp,
            (foc_sat_comp_mode_t)self->config.sat_comp_mode, self->config.r_ohm, false,
            self->config.observer_type, &r_eff, &l_eff, &lambda_eff);

        foc_observer_update(&self->observer, self->v_alpha, self->v_beta, i_alpha, i_beta, dt,
                            r_eff, l_eff, lambda_eff, self->config.observer_gamma,
                            self->config.observer_type);
        angle_rad = self->observer.phase;

        /* Reference: the observer's angle goes through the PLL, and the PLL speed
         * is what the control path uses. RADPS2RPM_f is electrical rpm, so the pole
         * pairs convert it to mechanical, as mc_interface_get_speed() does. */
        foc_pll_run(&self->pll, self->observer.phase, dt, self->config.pll_kp, self->config.pll_ki);
        rpm = (self->pll.speed * 60.0f / (2.0f * (float)M_PI)) /
              ((float)self->config.si_motor_poles / 2.0f);
    } else {
        st = self->rotor_sensor->read_angle(self->rotor_sensor->self, &angle_rad, &rpm);
        if (st != EDGE_OK) {
            self->faults |= FOC_FAULT_SENSOR_LOST;
            self->state = FOC_STATE_FAULT;
            (void)self->inverter->set_phase_state(self->inverter->self, false);
            return st;
        }
    }

    /*
     * Handbrake: the reference forces the electrical phase to zero in this mode so the
     * current simply locks the rotor (mcpwm_foc.c:3602). Forcing it here, before the
     * angle is stored, also makes the reported phase zero, as the reference's assignment
     * to state_now->phase does, and freezes the sector-based tachometer with it.
     */
    if (self->state == FOC_STATE_HANDBRAKE) {
        angle_rad = 0.0f;
    }

    self->last_angle_rad = angle_rad;
    self->last_rpm = rpm;

    /* Tachometer, reference mcpwm_foc.c:3866-3881. Phase normalised to [-pi, pi),
     * quantised to six 60-degree sectors, differenced against the previous sector.
     * The two corrections are the sector-wrap fixups: going 5 -> 0 is one step
     * forward, not five back. */
    float ph_tmp = angle_rad;
    while (ph_tmp < -(float)M_PI) {
        ph_tmp += 2.0f * (float)M_PI;
    }
    while (ph_tmp >= (float)M_PI) {
        ph_tmp -= 2.0f * (float)M_PI;
    }
    int step = (int)floorf((ph_tmp + (float)M_PI) / (2.0f * (float)M_PI) * 6.0f);
    if (step > 5) {
        step = 5;
    } else if (step < 0) {
        step = 0;
    }
    int diff = step - self->tacho_step_last;
    self->tacho_step_last = step;

    if (diff > 3) {
        diff -= 6;
    } else if (diff < -2) {
        diff += 6;
    }

    self->tachometer += diff;
    self->tachometer_abs += (diff < 0) ? -diff : diff;

    /* 5. Park Transform */
    float sin_th = 0.0f, cos_th = 0.0f;
    foc_fast_sincos(angle_rad, &sin_th, &cos_th);

    float id = 0.0f, iq = 0.0f;
    foc_park_transform(i_alpha, i_beta, sin_th, cos_th, &id, &iq);
    self->last_id = id;
    self->last_iq = iq;

    /* Reference: UTILS_LP_FAST(id_filter, id, foc_current_filter_const) at
     * mcpwm_foc.c:4628, which is `value -= c * (value - sample)`. The additive form
     * is algebraically equal but moves the last bits, and this filter feeds the
     * energy-counter gate. */
    self->id_filter -= self->config.current_filter_const * (self->id_filter - id);
    self->iq_filter -= self->config.current_filter_const * (self->iq_filter - iq);

    /* 6. Current Safety Invariant Checks */
    float current_mag = sqrtf(SQ(id) + SQ(iq));
    if (current_mag > self->config.current_max_a * 1.5f && self->config.current_max_a > 0.0f) {
        self->faults |= FOC_FAULT_OVER_CURRENT;
        self->state = FOC_STATE_FAULT;
        (void)self->inverter->set_phase_state(self->inverter->self, false);
        return EDGE_EBUSY;
    }

    /* 7. State Machine Execution */
    if (self->state == FOC_STATE_UNINITIALIZED || self->state == FOC_STATE_FAULT ||
        self->state == FOC_STATE_IDLE) {
        (void)self->inverter->set_phase_state(self->inverter->self, false);
        self->duty_a = 0.5f;
        self->duty_b = 0.5f;
        self->duty_c = 0.5f;
        return EDGE_OK;
    }

    /* 8. Closed-loop Controllers */
    float vd = 0.0f, vq = 0.0f;
    if (self->state == FOC_STATE_RUNNING_RPM) {
        /*
         * Reference: foc_run_pid_control_speed. It works in ERPM, and COMM_SET_RPM's
         * setpoint is ERPM too; the rotor/PLL speed here is mechanical, hence the
         * pole-pair conversion. `index_found` is passed true: the reference uses it
         * to clamp the setpoint to openloop_rpm only when no encoder index has been
         * seen, and this port has no index to lose.
         */
        float rpm_erpm = rpm * ((float)self->config.si_motor_poles / 2.0f);
        float iq_cmd = self->target_iq;
        foc_run_pid_speed(&self->speed_pid, &self->config.speed_pid, true, true, rpm_erpm,
                          self->target_rpm, dt, &iq_cmd);
        self->target_iq = iq_cmd;
    } else if (self->state == FOC_STATE_RUNNING_POS) {
        /* Position PID loop to compute target_iq */
        float current_deg = angle_rad * (180.0f / (float)M_PI);
        float err_pos = self->target_rpm - current_deg; /* using target_rpm as target_pos */
        float iq_cmd = err_pos * 0.1f;
        if (iq_cmd > self->config.current_max_a) {
            iq_cmd = self->config.current_max_a;
        }
        if (iq_cmd < self->config.current_min_a) {
            iq_cmd = self->config.current_min_a;
        }
        self->target_iq = iq_cmd;
    }

    if (self->state == FOC_STATE_RUNNING_CURRENT || self->state == FOC_STATE_RUNNING_RPM ||
        self->state == FOC_STATE_RUNNING_POS || self->state == FOC_STATE_HANDBRAKE) {
        /*
         * The reference applies MTPA and field weakening to this cycle's *setpoints* rather
         * than to the command, so they are locals here too: target_id/target_iq stay what the
         * command set, and id_set/iq_set are what the PI tracks (mcpwm_foc.c:3627-3654).
         */
        float iq_set = self->target_iq;
        float id_set = self->target_id;
        foc_apply_mtpa(self->config.mtpa_mode, self->config.ld_lq_diff, self->config.lambda_wb,
                       self->iq_filter, &iq_set, &id_set);

        const foc_fw_state_t fw_state = {.duty_abs_filtered = self->duty_abs_filtered,
                                         .iq = iq,
                                         .iq_target = self->target_iq,
                                         .speed_erpm =
                                             rpm * ((float)self->config.si_motor_poles / 2.0f),
                                         .i_fw_set = self->i_fw_set};
        const foc_fw_params_t fw_params = {.current_max = self->config.fw_current_max,
                                           .duty_start = self->config.fw_duty_start,
                                           .backoff = self->config.fw_backoff,
                                           .ramp_time = self->config.fw_ramp_time,
                                           .l_max_duty = self->config.duty_max,
                                           .cc_min_current = self->config.cc_min_current};
        /* The reference's mode gate includes the braking mode; this port's brake is
         * approximated by the current mode (see docs/adr-conformance.md), so it is included
         * implicitly rather than named. */
        const bool fw_mode_allows =
            (self->state == FOC_STATE_RUNNING_CURRENT || self->state == FOC_STATE_RUNNING_RPM);
        foc_fw_state_t fw_next = fw_state;
        foc_run_fw(&fw_next, &fw_params, fw_mode_allows, dt);
        self->i_fw_set = fw_next.i_fw_set;

        id_set = foc_max_abs(id_set, -self->i_fw_set);
        iq_set -= foc_sign(self->mod_q_filter) * self->i_fw_set * self->config.fw_q_current_factor;

        /* d-axis PI controller */
        float err_d = id_set - id;
        self->id_integral += err_d * self->config.current_ki * dt;
        /* anti-windup clamp */
        /* mod = 1.5 * v / v_bus reaches 1.0 at v = (2/3) * v_bus, which is the
         * reference's definition of the largest vector the inverter can make. */
        float v_limit = (2.0f / 3.0f) * v_bus;
        if (self->id_integral > v_limit)
            self->id_integral = v_limit;
        if (self->id_integral < -v_limit)
            self->id_integral = -v_limit;
        vd = err_d * self->config.current_kp + self->id_integral;

        /* q-axis PI controller */
        float err_q = iq_set - iq;
        self->iq_integral += err_q * self->config.current_ki * dt;
        if (self->iq_integral > v_limit)
            self->iq_integral = v_limit;
        if (self->iq_integral < -v_limit)
            self->iq_integral = -v_limit;
        vq = err_q * self->config.current_kp + self->iq_integral;
    } else if (self->state == FOC_STATE_RUNNING_DUTY) {
        vd = 0.0f;
        vq = self->target_duty * v_bus * (2.0f / 3.0f);
    }

    self->v_d = vd;
    self->v_q = vq;

    /*
     * Duty cycle, reference mcpwm_foc.c:3818-3820:
     *   duty_now = SIGN(vq) * NORM2_f(mod_d, mod_q) * p_duty_norm
     * where mod = v * 1.5 / v_bus (mcpwm_foc.c:3806-3811) and
     * p_duty_norm = TWO_BY_SQRT3 / foc_overmod_factor. foc_overmod_factor defaults
     * to 1.0 in the reference (mcconf_default.h:512) and is not a field of this
     * configuration yet, so the constant stands in for p_duty_norm here; when the
     * field arrives, divide by it at this line.
     *
     * The unfiltered mod values are used, as the reference does - mod_q_filter is a
     * separate quantity it keeps alongside for other consumers.
     */
    const float mod_d = vd * (1.5f / v_bus);
    const float mod_q = vq * (1.5f / v_bus);
    self->duty_now = foc_sign(vq) * NORM2_f(mod_d, mod_q) * TWO_BY_SQRT3;

    /*
     * The two filters field weakening and MTPA consume (reference mcpwm_foc.c:3812-3814 and
     * :3332-3333): mod_q low-passed at 0.2, and |duty_now| at 0.01, each clamped to
     * magnitude 1. Both use UTILS_LP_FAST's `value -= c * (value - sample)` form, which is
     * what keeps them bit-equal to the reference.
     */
    self->mod_q_filter -= 0.2f * (self->mod_q_filter - mod_q);
    foc_truncate_number_abs(&self->mod_q_filter, 1.0f);
    self->duty_abs_filtered -= 0.01f * (self->duty_abs_filtered - fabsf(self->duty_now));
    foc_truncate_number_abs(&self->duty_abs_filtered, 1.0f);

    /* 9. Inverse Park Transform */
    float v_alpha = 0.0f, v_beta = 0.0f;
    foc_inv_park_transform(vd, vq, sin_th, cos_th, &v_alpha, &v_beta);
    self->v_alpha = v_alpha;
    self->v_beta = v_beta;

    /* 10. Space Vector Modulation (SVPWM) */
    float da = 0.5f, db = 0.5f, dc = 0.5f;
    uint32_t sector = 1u;
    foc_svpwm(v_alpha, v_beta, v_bus, self->config.duty_max, &da, &db, &dc, &sector);

    self->duty_a = da;
    self->duty_b = db;
    self->duty_c = dc;
    self->svm_sector = sector;

    /*
     * Input current and the energy counters. Reference: mcpwm_foc.c:4711-4718 for
     * i_abs/i_abs_filter/i_bus, mc_interface.c:2036-2039 for the counters.
     *
     * Known difference: the reference runs the counters on the periodic MC timer
     * with that timer's dt, not in the FOC loop. The integrated quantity is the
     * same (integral of current over time) and the port has no separate timer
     * tick carrying a dt, so it accumulates here with the loop dt.
     */
    self->i_abs = sqrtf(SQ(id) + SQ(iq));
    self->i_abs_filter = sqrtf(SQ(self->id_filter) + SQ(self->iq_filter));
    if (v_bus > 0.0f) {
        self->i_bus = 1.5f * (vd * id + vq * iq) / v_bus;
    }

    if (fabsf(self->i_abs_filter) > 1.0f) {
        if (self->i_bus > 0.0f) {
            self->amp_seconds += self->i_bus * dt;
            self->watt_seconds += self->i_bus * dt * v_bus;
        } else {
            self->amp_seconds_charged -= self->i_bus * dt;
            self->watt_seconds_charged -= self->i_bus * dt * v_bus;
        }
    }

    /* 11. Output to Inverter */
    (void)self->inverter->set_phase_state(self->inverter->self, true);
    return self->inverter->set_duty(self->inverter->self, da, db, dc);
}

void foc_core_read_reset_averages(foc_core_t *self, uint32_t channel_mask, foc_averages_t *out) {
    if (self == (void *)0 || out == (void *)0) {
        return;
    }

    memset(out, 0, sizeof(*out));

    /* Division by the iteration count, then reset - the reference's arithmetic,
     * including its 0/0 when nothing was sampled since the previous read. */
    if (channel_mask & FOC_AVG_MOTOR_CURRENT) {
        out->motor_current = self->avg_motor_current_sum / self->avg_motor_current_iterations;
        self->avg_motor_current_sum = 0.0f;
        self->avg_motor_current_iterations = 0.0f;
    }
    if (channel_mask & FOC_AVG_INPUT_CURRENT) {
        out->input_current = self->avg_input_current_sum / self->avg_input_current_iterations;
        self->avg_input_current_sum = 0.0f;
        self->avg_input_current_iterations = 0.0f;
    }
    if (channel_mask & FOC_AVG_ID) {
        out->id = self->avg_id_sum / self->avg_id_iterations;
        self->avg_id_sum = 0.0f;
        self->avg_id_iterations = 0.0f;
    }
    if (channel_mask & FOC_AVG_IQ) {
        out->iq = self->avg_iq_sum / self->avg_iq_iterations;
        self->avg_iq_sum = 0.0f;
        self->avg_iq_iterations = 0.0f;
    }
    if (channel_mask & FOC_AVG_VD) {
        out->vd = self->avg_vd_sum / self->avg_vd_iterations;
        self->avg_vd_sum = 0.0f;
        self->avg_vd_iterations = 0.0f;
    }
    if (channel_mask & FOC_AVG_VQ) {
        out->vq = self->avg_vq_sum / self->avg_vq_iterations;
        self->avg_vq_sum = 0.0f;
        self->avg_vq_iterations = 0.0f;
    }
}

edge_status_t foc_core_set_current(foc_core_t *self, float iq_target, float id_target) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    if (self->state == FOC_STATE_FAULT || self->state == FOC_STATE_UNINITIALIZED) {
        return EDGE_EBUSY;
    }

    /*
     * No clamp, deliberately. The reference does not limit here: mc_interface_set_current
     * only runs SHUTDOWN_RESET, the input gate, DIR_MULT and events_add, and
     * mcpwm_foc_set_current stores the value as given (mcpwm_foc.c:800-816). Limiting
     * happens in the apps and in field weakening, and this port used to clamp to
     * [current_min_a, current_max_a], which the reference never does - a 60 A
     * configuration commanded to 100 A holds 100 A there, not 60 A.
     *
     * The reference also has a cc_min_current guard in the same function, but it only
     * skips the MC-state transition and the m_motor_released clear, both of which are
     * field-weakening concerns; the control mode and the setpoints are written before
     * it. Here the state field IS the control mode, so that guard has no counterpart.
     */
    self->target_iq = iq_target;
    self->target_id = id_target;
    self->state = FOC_STATE_RUNNING_CURRENT;

    return EDGE_OK;
}

edge_status_t foc_core_set_current_rel(foc_core_t *self, float rel) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * Reference mc_interface_set_current_rel (mc_interface.c:733-749). The limit base
     * depends on the duty's sign: as long as the machine is near standstill
     * (|duty| < 0.02) or the setpoint pushes the same way as the duty, the positive
     * limit applies, otherwise the negative one. SIGN is +1.0 at exactly zero, so a
     * zero setpoint against a negative duty picks the negative limit - the reference's
     * behaviour, not an accident to fix.
     */
    const float base = (fabsf(self->duty_now) < 0.02f || foc_sign(rel) == foc_sign(self->duty_now))
                           ? self->config.current_max_a
                           : fabsf(self->config.current_min_a);

    /*
     * The reference then calls mc_interface_set_current(), so DIR_MULT and the rest of
     * that path apply - DIR_MULT is the glue's job here. Its trailing
     * set_current_off_delay(0.1), gated by l_abs_current_max and cc_min_current, only
     * feeds the field-weakening modulation extension (mcpwm_foc.c:3953/3970 read
     * m_current_off_delay, and m_motor_released with it); nothing reads such a delay
     * here, so it is recorded as pending B3 rather than carried as dead state.
     */
    return foc_core_set_current(self, rel * base, 0.0f);
}

edge_status_t foc_core_set_rpm(foc_core_t *self, float rpm_target) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    if (self->state == FOC_STATE_FAULT || self->state == FOC_STATE_UNINITIALIZED) {
        return EDGE_EBUSY;
    }

    self->target_rpm = rpm_target;
    self->target_id = 0.0f;
    self->state = FOC_STATE_RUNNING_RPM;
    return EDGE_OK;
}

edge_status_t foc_core_set_pos(foc_core_t *self, float pos_target_deg) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    if (self->state == FOC_STATE_FAULT || self->state == FOC_STATE_UNINITIALIZED) {
        return EDGE_EBUSY;
    }

    self->target_rpm = pos_target_deg; /* Store pos setpoint */
    self->target_id = 0.0f;
    self->state = FOC_STATE_RUNNING_POS;
    return EDGE_OK;
}

edge_status_t foc_core_set_handbrake(foc_core_t *self, float brake_current_a) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    if (self->state == FOC_STATE_FAULT || self->state == FOC_STATE_UNINITIALIZED) {
        return EDGE_EBUSY;
    }

    /*
     * Reference mcpwm_foc_set_handbrake (mcpwm_foc.c:854-864). Three things this used to
     * get wrong, all of which made it a different command: the reference does NOT take
     * the absolute value (the sign is the caller's, and COMM_SET_HANDBRAKE's own
     * relative form is what makes it positive), it writes the *q* axis, and it enters a
     * mode of its own - which is what the loop's phase override acts on. It also applies
     * no DIR_MULT, unlike current/brake/duty/rpm.
     */
    self->target_iq = brake_current_a;
    self->state = FOC_STATE_HANDBRAKE;
    return EDGE_OK;
}

edge_status_t foc_core_set_duty(foc_core_t *self, float duty_target) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    if (self->state == FOC_STATE_FAULT || self->state == FOC_STATE_UNINITIALIZED) {
        return EDGE_EBUSY;
    }

    if (duty_target > self->config.duty_max)
        duty_target = self->config.duty_max;
    if (duty_target < -self->config.duty_max)
        duty_target = -self->config.duty_max;

    self->target_duty = duty_target;
    self->state = FOC_STATE_RUNNING_DUTY;

    return EDGE_OK;
}

edge_status_t foc_core_stop(foc_core_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    self->target_id = 0.0f;
    self->target_iq = 0.0f;
    self->target_duty = 0.0f;
    self->id_integral = 0.0f;
    self->iq_integral = 0.0f;

    if (self->state != FOC_STATE_FAULT && self->state != FOC_STATE_UNINITIALIZED) {
        self->state = FOC_STATE_IDLE;
    }

    if (self->inverter != (void *)0 && self->inverter->set_phase_state != (void *)0) {
        (void)self->inverter->set_phase_state(self->inverter->self, false);
    }

    return EDGE_OK;
}

edge_status_t foc_core_clear_faults(foc_core_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    self->faults = FOC_FAULT_NONE;
    if (self->state == FOC_STATE_FAULT) {
        self->state = FOC_STATE_IDLE;
    }
    self->id_integral = 0.0f;
    self->iq_integral = 0.0f;

    return EDGE_OK;
}

foc_state_t foc_core_get_state(const foc_core_t *self) {
    return (self != (void *)0) ? self->state : FOC_STATE_UNINITIALIZED;
}

uint32_t foc_core_get_faults(const foc_core_t *self) {
    return (self != (void *)0) ? self->faults : 0u;
}

void foc_core_get_telemetry(const foc_core_t *self, foc_telemetry_t *out_telem) {
    if (self == (void *)0 || out_telem == (void *)0) {
        return;
    }

    out_telem->state = self->state;
    out_telem->faults = self->faults;
    out_telem->v_bus = self->last_v_bus;
    out_telem->current_d = self->last_id;
    out_telem->current_q = self->last_iq;
    out_telem->current_abs = sqrtf(SQ(self->last_id) + SQ(self->last_iq));
    /* Reference mcpwm_foc.c:3818; NOT duty_a, which is a phase duty. */
    out_telem->duty_now = self->duty_now;
    out_telem->rotor_angle_rad = self->last_angle_rad;
    out_telem->speed_rpm = self->last_rpm;
    out_telem->fet_temp_c = self->fet_temp_c;
    out_telem->tachometer = self->tachometer;
    out_telem->tachometer_abs = self->tachometer_abs;
    out_telem->current_in = self->i_bus;
    out_telem->amp_hours = self->amp_seconds / 3600.0f;
    out_telem->amp_hours_charged = self->amp_seconds_charged / 3600.0f;
    out_telem->watt_hours = self->watt_seconds / 3600.0f;
    out_telem->watt_hours_charged = self->watt_seconds_charged / 3600.0f;
}

void foc_core_get_stats(const foc_core_t *self, foc_stats_t *out_stats) {
    if (self == (void *)0 || out_stats == (void *)0) {
        return;
    }

    /* sum/samples, with the reference's 0/0 when nothing was sampled yet. */
    out_stats->speed_avg = self->stat_speed_sum / self->stat_samples;
    out_stats->speed_max = self->stat_max_speed;
    out_stats->power_avg = self->stat_power_sum / self->stat_samples;
    out_stats->current_avg = self->stat_current_sum / self->stat_samples;
    out_stats->temp_mos_avg = self->stat_temp_mos_sum / self->stat_samples;
    out_stats->temp_motor_avg = self->stat_temp_motor_sum / self->stat_samples;

    out_stats->power_max = self->stat_max_power;
    out_stats->current_max = self->stat_max_current;
    out_stats->temp_mos_max = self->stat_max_temp_mos;
    out_stats->temp_motor_max = self->stat_max_temp_motor;
}

void foc_core_stats_reset(foc_core_t *self) {
    if (self == (void *)0) {
        return;
    }

    /* Reference: mc_interface_stat_reset() - zero the sums, and put the two
     * temperature maxima back to -300. */
    self->stat_samples = 0.0f;
    self->stat_power_sum = 0.0f;
    self->stat_max_power = 0.0f;
    self->stat_current_sum = 0.0f;
    self->stat_max_current = 0.0f;
    self->stat_temp_mos_sum = 0.0f;
    self->stat_temp_motor_sum = 0.0f;
    self->stat_max_temp_mos = -300.0f;
    self->stat_max_temp_motor = -300.0f;
}

void foc_core_set_temperature(foc_core_t *self, float fet_temp_c) {
    if (self != (void *)0) {
        self->fet_temp_c = fet_temp_c;
    }
}
