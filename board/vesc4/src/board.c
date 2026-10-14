#include "vesc4/board.h"
#include <string.h>

edge_status_t board_vesc4_init(board_vesc4_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    /* Amps per ADC count = (Vref / MaxCount) / (Shunt * Gain) */
    self->current_scale =
        (VESC4_ADC_VREF_V / VESC4_ADC_MAX_COUNT) / (VESC4_SHUNT_RES_OHM * VESC4_CURRENT_AMP_GAIN);

    /* Volts per ADC count = (Vref / MaxCount) * ((R1 + R2) / R2) */
    self->voltage_scale = (VESC4_ADC_VREF_V / VESC4_ADC_MAX_COUNT) *
                          ((VESC4_VOLTAGE_DIV_R1 + VESC4_VOLTAGE_DIV_R2) / VESC4_VOLTAGE_DIV_R2);

    self->initialized = true;
    return EDGE_OK;
}

float board_vesc4_get_current_scale(const board_vesc4_t *self) {
    return self != (void *)0 ? self->current_scale : 0.0f;
}

float board_vesc4_get_voltage_scale(const board_vesc4_t *self) {
    return self != (void *)0 ? self->voltage_scale : 0.0f;
}

float board_vesc4_calc_current_a(const board_vesc4_t *self, int32_t adc_counts_diff) {
    if (self == (void *)0) {
        return 0.0f;
    }
    return (float)adc_counts_diff * self->current_scale;
}

float board_vesc4_calc_voltage_v(const board_vesc4_t *self, uint16_t adc_raw) {
    if (self == (void *)0) {
        return 0.0f;
    }
    return (float)adc_raw * self->voltage_scale;
}
