#ifndef BOARD_VESC_UNITY_H
#define BOARD_VESC_UNITY_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VESC_UNITY_HW_NAME "VESC_UNITY"
#define VESC_UNITY_SHUNT_RES_OHM 0.0005f
#define VESC_UNITY_CURRENT_AMP_GAIN 20.0f
#define VESC_UNITY_VOLTAGE_DIV_R1 39000.0f
#define VESC_UNITY_VOLTAGE_DIV_R2 2200.0f
#define VESC_UNITY_ADC_VREF_V 3.3f
#define VESC_UNITY_ADC_MAX_COUNT 4095.0f

typedef struct board_vesc_unity {
    float current_scale;
    float voltage_scale;
    bool dual_motor_enabled;
    bool initialized;
} board_vesc_unity_t;

edge_status_t board_vesc_unity_init(board_vesc_unity_t *self);
float board_vesc_unity_get_current_scale(const board_vesc_unity_t *self);
float board_vesc_unity_get_voltage_scale(const board_vesc_unity_t *self);
float board_vesc_unity_calc_current_a(const board_vesc_unity_t *self, int32_t adc_counts_diff);
float board_vesc_unity_calc_voltage_v(const board_vesc_unity_t *self, uint16_t adc_raw);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_VESC_UNITY_H */
