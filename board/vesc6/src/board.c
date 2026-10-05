#include "vesc6/board.h"
#include <string.h>

edge_status_t board_vesc6_init(board_vesc6_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* Amps per ADC count = (Vref / MaxCount) / (Shunt * Gain) */
    self->current_scale =
        (VESC6_ADC_VREF_V / VESC6_ADC_MAX_COUNT) / (VESC6_SHUNT_RES_OHM * VESC6_CURRENT_AMP_GAIN);

    /* Volts per ADC count = (Vref / MaxCount) * ((R1 + R2) / R2) */
    self->voltage_scale = (VESC6_ADC_VREF_V / VESC6_ADC_MAX_COUNT) *
                          ((VESC6_VOLTAGE_DIV_R1 + VESC6_VOLTAGE_DIV_R2) / VESC6_VOLTAGE_DIV_R2);

    self->initialized = true;
    return EDGE_OK;
}

float board_vesc6_get_current_scale(const board_vesc6_t *self) {
    return self != (void *)0 ? self->current_scale : 0.0f;
}

float board_vesc6_get_voltage_scale(const board_vesc6_t *self) {
    return self != (void *)0 ? self->voltage_scale : 0.0f;
}

float board_vesc6_calc_current_a(const board_vesc6_t *self, int32_t adc_counts_diff) {
    if (self == (void *)0) {
        return 0.0f;
    }
    return (float)adc_counts_diff * self->current_scale;
}

float board_vesc6_calc_voltage_v(const board_vesc6_t *self, uint16_t adc_raw) {
    if (self == (void *)0) {
        return 0.0f;
    }
    return (float)adc_raw * self->voltage_scale;
}

/*
 * The step-to-channel table, read straight off set_next_comm_step's six branches
 * (mcpwm.c:2694-3010), in the timer's channel order: each row is one step, the first for the
 * direction the reference calls true and the second for its opposite. The roles are the comments
 * its branches carry, and the channel a step does not drive is the one it sets inactive first.
 */
static const uint8_t step_channels[6][2][3] = {
    {{BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE},
     {BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE}},
    {{BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE},
     {BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT}},
    {{BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT},
     {BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE}},
    {{BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE},
     {BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_NEGATIVE}},
    {{BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE},
     {BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT}},
    {{BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_POSITIVE, BOARD_VESC6_PHASE_FLOAT},
     {BOARD_VESC6_PHASE_NEGATIVE, BOARD_VESC6_PHASE_FLOAT, BOARD_VESC6_PHASE_POSITIVE}},
};

void board_vesc6_comm_step_channels(int step, bool direction, board_vesc6_phase_role_t out[3]) {
    if (out == (void *)0) {
        return;
    }

    if (step < 1 || step > 6) {
        /*
         * The reference's branch chain ends without a match: it forces the first channel inactive
         * and says nothing about the other two, which stay as the timer has them.
         */
        out[0] = BOARD_VESC6_PHASE_FLOAT;
        out[1] = BOARD_VESC6_PHASE_FLOAT;
        out[2] = BOARD_VESC6_PHASE_FLOAT;
        return;
    }

    const uint8_t(*row)[3] = step_channels[step - 1];
    for (int i = 0; i < 3; i++) {
        out[i] = (board_vesc6_phase_role_t)row[direction ? 0 : 1][i];
    }
}

void board_vesc6_comm_step_settings(board_vesc6_phase_role_t role, board_vesc6_pwm_mode_t pwm_mode,
                                    bool detecting, board_vesc6_phase_out_t *out) {
    if (out == (void *)0) {
        return;
    }

    /* The four locals the reference sets before its per-step branches (mcpwm.c:2718-2729). */
    const board_vesc6_oc_mode_t positive_oc_mode = BOARD_VESC6_OC_PWM1;
    bool positive_lowside = true;
    board_vesc6_oc_mode_t negative_oc_mode = BOARD_VESC6_OC_INACTIVE;
    const bool negative_lowside = true;

    /* :2731-2749: the pulse mode is only applied when nothing is being detected. */
    if (!detecting) {
        switch (pwm_mode) {
        case BOARD_VESC6_PWM_NONSYNCHRONOUS_HISW:
            positive_lowside = false;
            break;
        case BOARD_VESC6_PWM_BIPOLAR:
            negative_oc_mode = BOARD_VESC6_OC_PWM2;
            break;
        case BOARD_VESC6_PWM_SYNCHRONOUS:
        default:
            break;
        }
    }

    switch (role) {
    case BOARD_VESC6_PHASE_POSITIVE:
        out->oc_mode = positive_oc_mode;
        out->ccx_enable = true;
        out->ccxn_enable = positive_lowside;
        break;
    case BOARD_VESC6_PHASE_NEGATIVE:
        out->oc_mode = negative_oc_mode;
        out->ccx_enable = true;
        out->ccxn_enable = negative_lowside;
        break;
    case BOARD_VESC6_PHASE_FLOAT:
    default:
        /* The reference's "0" channel, whatever the pulse mode. */
        out->oc_mode = BOARD_VESC6_OC_INACTIVE;
        out->ccx_enable = true;
        out->ccxn_enable = false;
        break;
    }
}
