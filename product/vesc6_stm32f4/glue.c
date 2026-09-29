#include "glue.h"

#include <string.h>

/*
 * Adapter layer (D14): motor_config owns its consumer-defined variable port, foc_core owns the
 * ports its control loop drives, and infra and the soc expose their own concrete APIs. The
 * composition root bridges them here.
 *
 * The store is a table in RAM. The reference keeps the same words in the EEPROM emulation over
 * flash, and this port is where that goes once the flash driver exists - soc/stm32f4 has no flash
 * driver yet, so nothing here may claim to persist. What this port does carry is the part that is
 * not hardware: the read/write protocol, including the failing read for a word never written.
 */
static edge_status_t var_read(void *self, uint16_t index, uint16_t *value) {
    vesc6_glue_state_t *state = (vesc6_glue_state_t *)self;
    if (state == NULL || value == NULL || index >= VESC6_MCCONF_VARS) {
        return EDGE_EINVAL;
    }
    if (state->written[index] == 0u) {
        return EDGE_ENOENT;
    }
    *value = state->values[index];
    return EDGE_OK;
}

static edge_status_t var_write(void *self, uint16_t index, uint16_t value) {
    vesc6_glue_state_t *state = (vesc6_glue_state_t *)self;
    if (state == NULL || index >= VESC6_MCCONF_VARS) {
        return EDGE_EINVAL;
    }
    state->values[index] = value;
    state->written[index] = 1u;
    return EDGE_OK;
}

/*
 * The control loop's duties are fractions of the period (foc_core starts at 0.5, i.e. mid-scale),
 * and the timer's compare registers take counts. Rounding is to the nearest count: truncating puts
 * every phase half a count low, which is a constant offset rather than noise.
 */
static uint32_t duty_to_counts(float duty, uint32_t period) {
    if (duty <= 0.0f) {
        return 0u;
    }
    if (duty >= 1.0f) {
        return period;
    }
    return (uint32_t)(duty * (float)period + 0.5f);
}

static edge_status_t inverter_set_duty(void *self, float duty_a, float duty_b, float duty_c) {
    vesc6_glue_state_t *state = (vesc6_glue_state_t *)self;
    if (state == NULL || state->tim == NULL || state->period == 0u) {
        return EDGE_EINVAL;
    }
    /* TIM1 drives the phases in the order they are named here. The 1, 3, 2 order the reference
     * writes is the other timer's (motor/mcpwm_foc.c:94-104). */
    return soc_stm32f4_tim_set_duty_counts(state->tim, duty_to_counts(duty_a, state->period),
                                           duty_to_counts(duty_b, state->period),
                                           duty_to_counts(duty_c, state->period), false)
               ? EDGE_OK
               : EDGE_EINVAL;
}

static edge_status_t inverter_set_phase_state(void *self, bool enable) {
    vesc6_glue_state_t *state = (vesc6_glue_state_t *)self;
    if (state == NULL || state->tim == NULL) {
        return EDGE_EINVAL;
    }
    if (enable) {
        /*
         * Refused while the board's dead time is unknown. MOE is what puts voltage on the phases,
         * and both switches of one half-bridge on at once is a short, so the dead time has to be a
         * value the board stands behind rather than one this file picks. The VESC6's own is not in
         * this checkout - the 60 family configures its timer through ChibiOS rather than through
         * HW_DEAD_TIME_NSEC - so the composition root fills it in, and until it does the outputs
         * stay off.
         */
        if (state->deadtime_ns <= 0.0f) {
            return EDGE_ENOTSUP;
        }
        soc_stm32f4_tim_enable_outputs(state->tim);
        return EDGE_OK;
    }
    soc_stm32f4_tim_disable_outputs(state->tim);
    return EDGE_OK;
}

/*
 * One phase current from the snapshot the interrupt fills. The three ranks are three samples of
 * the same channel - what the reference's own setup asks for - so they are averaged, and the sum
 * is a count around mid-scale that the board's function is defined over.
 */
static edge_status_t current_read(void *self, float *ia, float *ib, float *ic) {
    vesc6_glue_state_t *state = (vesc6_glue_state_t *)self;
    if (state == NULL || state->board == NULL || ia == NULL || ib == NULL || ic == NULL) {
        return EDGE_EINVAL;
    }

    float *const out[3] = {ia, ib, ic};
    for (uint32_t phase = 0u; phase < 3u; ++phase) {
        uint32_t sum = 0u;
        for (uint32_t rank = 0u; rank < VESC6_CURRENT_RANKS; ++rank) {
            sum += state->current_counts[phase][rank];
        }
        const uint32_t counts = sum / VESC6_CURRENT_RANKS;
        if (counts < VESC6_CURRENT_MID_SCALE) {
            *out[phase] = board_vesc6_calc_current_a(state->board,
                                                     -(int32_t)(VESC6_CURRENT_MID_SCALE - counts));
        } else {
            *out[phase] = board_vesc6_calc_current_a(state->board,
                                                     (int32_t)(counts - VESC6_CURRENT_MID_SCALE));
        }
    }
    return EDGE_OK;
}

void vesc6_adc_injected_hook(void *ctx, uint32_t adc_index) {
    vesc6_glue_state_t *state = (vesc6_glue_state_t *)ctx;
    if (state == NULL || adc_index >= 3u || state->adc_current[adc_index] == NULL) {
        return;
    }
    (void)soc_stm32f4_adc_read_injected(state->adc_current[adc_index], VESC6_CURRENT_RANKS,
                                        state->current_counts[adc_index]);
}

static edge_status_t current_read_vbus(void *self, float *v_bus) {
    vesc6_glue_state_t *state = (vesc6_glue_state_t *)self;
    if (state == NULL || state->board == NULL || state->adc_vbus == NULL || v_bus == NULL) {
        return EDGE_EINVAL;
    }
    uint16_t raw = 0u;
    if (!soc_stm32f4_adc_read_regular(state->adc_vbus, &raw)) {
        return EDGE_EIO;
    }
    *v_bus = board_vesc6_calc_voltage_v(state->board, raw);
    return EDGE_OK;
}

void vesc6_glue_init(vesc6_glue_state_t *self, const board_vesc6_t *board) {
    if (self == NULL) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->board = board;
}

void vesc6_make_var_port(motor_config_var_port_t *out, vesc6_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    *out = (motor_config_var_port_t){
        .read = var_read,
        .write = var_write,
        .self = state,
    };
}

static edge_status_t rotor_read_angle(void *self, float *angle_rad, float *rpm) {
    (void)self;
    (void)angle_rad;
    (void)rpm;
    /* A hall or encoder driver is what would fill these in, and this board has neither yet. An
     * angle is the one thing in this file that must not be approximated: the control loop acts on
     * it directly. */
    return EDGE_ENOTSUP;
}

void vesc6_make_rotor_port(foc_rotor_port_t *out, vesc6_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    *out = (foc_rotor_port_t){
        .read_angle = rotor_read_angle,
        .self = state,
    };
}

void vesc6_make_inverter_port(foc_inverter_port_t *out, vesc6_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    *out = (foc_inverter_port_t){
        .set_duty = inverter_set_duty,
        .set_phase_state = inverter_set_phase_state,
        .self = state,
    };
}

void vesc6_make_current_port(foc_current_port_t *out, vesc6_glue_state_t *state) {
    if (out == NULL) {
        return;
    }
    *out = (foc_current_port_t){
        .read_currents = current_read,
        .read_vbus = current_read_vbus,
        .self = state,
    };
}
