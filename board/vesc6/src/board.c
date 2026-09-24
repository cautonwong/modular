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
