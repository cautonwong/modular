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

/*
 * mcpwm.c:2694-3010, set_next_comm_step: which of the timer's three channels each commutation step
 * drives, and how. The reference writes TIM1's registers itself; what its branches decide, and what
 * this board is asked for, is one of three roles for each channel.
 */
typedef enum board_vesc6_phase_role {
    BOARD_VESC6_PHASE_FLOAT = 0, /* its "0": inactive, output enabled, complementary output off */
    BOARD_VESC6_PHASE_POSITIVE,  /* its "+": the positive pulse on both outputs' own enables */
    BOARD_VESC6_PHASE_NEGATIVE   /* its "-": the negative mode, whose outputs the pwm mode gates */
} board_vesc6_phase_role_t;

/* The output compare mode one channel is selected into (TIM_SelectOCxM). */
typedef enum board_vesc6_oc_mode {
    BOARD_VESC6_OC_INACTIVE = 0,
    BOARD_VESC6_OC_PWM1,
    BOARD_VESC6_OC_PWM2,
    BOARD_VESC6_OC_FORCED_INACTIVE
} board_vesc6_oc_mode_t;

/* mcconf pwm_mode: the three the reference switches on when it is not detecting. */
typedef enum board_vesc6_pwm_mode {
    BOARD_VESC6_PWM_NONSYNCHRONOUS_HISW = 0,
    BOARD_VESC6_PWM_SYNCHRONOUS,
    BOARD_VESC6_PWM_BIPOLAR
} board_vesc6_pwm_mode_t;

/* One channel's settings for a step, which is the reference's TIM_SelectOCxM/CCx/CCxN triple. */
typedef struct board_vesc6_phase_out {
    board_vesc6_oc_mode_t oc_mode;
    bool ccx_enable;
    bool ccxn_enable;
} board_vesc6_phase_out_t;

/*
 * The role each channel takes on a step, in the timer's own order: out[0] is channel 1. A step
 * outside one to six leaves the reference's own fallback - the first channel forced inactive with
 * its output enabled, and nothing said about the other two - which is what its branch chain ends
 * on.
 */
void board_vesc6_comm_step_channels(int step, bool direction, board_vesc6_phase_role_t out[3]);

/*
 * What a role becomes once the pulse mode and the detecting flag are applied, which is the four
 * locals the reference sets before its per-step branches and the switch inside them.
 */
void board_vesc6_comm_step_settings(board_vesc6_phase_role_t role, board_vesc6_pwm_mode_t pwm_mode,
                                    bool detecting, board_vesc6_phase_out_t *out);

edge_status_t board_vesc6_init(board_vesc6_t *self);
float board_vesc6_get_current_scale(const board_vesc6_t *self);
float board_vesc6_get_voltage_scale(const board_vesc6_t *self);
float board_vesc6_calc_current_a(const board_vesc6_t *self, int32_t adc_counts_diff);
float board_vesc6_calc_voltage_v(const board_vesc6_t *self, uint16_t adc_raw);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_VESC6_H */
