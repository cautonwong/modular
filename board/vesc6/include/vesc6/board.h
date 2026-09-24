#ifndef BOARD_VESC6_H
#define BOARD_VESC6_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VESC6_HW_NAME "VESC6"
#define VESC6_SHUNT_RES_OHM 0.0005f
#define VESC6_CURRENT_AMP_GAIN 20.0f
#define VESC6_VOLTAGE_DIV_R1 39000.0f
#define VESC6_VOLTAGE_DIV_R2 2200.0f
#define VESC6_ADC_VREF_V 3.3f
#define VESC6_ADC_MAX_COUNT 4095.0f

typedef struct board_vesc6 {
    float current_scale;
    float voltage_scale;
    bool initialized;
} board_vesc6_t;

edge_status_t board_vesc6_init(board_vesc6_t *self);
float board_vesc6_get_current_scale(const board_vesc6_t *self);
float board_vesc6_get_voltage_scale(const board_vesc6_t *self);
float board_vesc6_calc_current_a(const board_vesc6_t *self, int32_t adc_counts_diff);
float board_vesc6_calc_voltage_v(const board_vesc6_t *self, uint16_t adc_raw);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_VESC6_H */
