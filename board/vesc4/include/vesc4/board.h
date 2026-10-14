#ifndef BOARD_VESC4_H
#define BOARD_VESC4_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VESC4_HW_NAME "VESC4"
#define VESC4_SHUNT_RES_OHM 0.001f
#define VESC4_CURRENT_AMP_GAIN 10.0f
#define VESC4_VOLTAGE_DIV_R1 39000.0f
#define VESC4_VOLTAGE_DIV_R2 2200.0f
#define VESC4_ADC_VREF_V 3.3f
#define VESC4_ADC_MAX_COUNT 4095.0f

typedef struct board_vesc4 {
    float current_scale;
    float voltage_scale;
    bool initialized;
} board_vesc4_t;

edge_status_t board_vesc4_init(board_vesc4_t *self);
float board_vesc4_get_current_scale(const board_vesc4_t *self);
float board_vesc4_get_voltage_scale(const board_vesc4_t *self);
float board_vesc4_calc_current_a(const board_vesc4_t *self, int32_t adc_counts_diff);
float board_vesc4_calc_voltage_v(const board_vesc4_t *self, uint16_t adc_raw);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_VESC4_H */
