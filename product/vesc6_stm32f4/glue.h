#ifndef PRODUCT_VESC6_STM32F4_GLUE_H
#define PRODUCT_VESC6_STM32F4_GLUE_H

#include "edge/errors.h"
#include "foc_core/foc_core.h"
#include "motor_config/motor_config.h"
#include "soc_stm32f4/soc_stm32f4.h"
#include "vesc6/board.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * The reference keeps its configuration in the EEPROM emulation, one u16 per configuration word,
 * starting at its own virtual base (conf_general.c:50 EEPROM_BASE_MCCONF = 1000, table built at
 * conf_general.c:72). That many words is what a store has to hold; the count is the size of the
 * reference's variable list, and it is the reason motor_config writes as many entries as it does.
 */
#define VESC6_MCCONF_VARS 388u

/*
 * Where the ADC reads zero amperes: mid-scale, as the reference's own arithmetic has it
 * (motor/virtual_motor.c:384 stores ia / FAC_CURRENT + 2048), and the board's scale is defined
 * over the difference from there.
 */
#define VESC6_CURRENT_MID_SCALE 2048u

/* How many injected ranks each current ADC fills. The reference's setup asks for three of the same
 * channel (hw_60_core.c:205-213), and the port hands on their average. */
#define VESC6_CURRENT_RANKS 3u

/*
 * The composition root's state (D1). The store below is a table in RAM until the flash driver
 * lands, and the board scales come from board/vesc6 rather than being re-derived here.
 *
 * Every entry carries a written flag because a stored word and an unstored one are not the same
 * value: the reference's EEPROM read fails for a word that was never written, and the
 * configuration relies on that failure to tell "no stored configuration" from "a configuration of
 * zeroes" - which is a real configuration, and its CRC is zero (see motor_config.h:42-46).
 */
typedef struct vesc6_glue_state {
    uint16_t values[VESC6_MCCONF_VARS];
    uint8_t written[VESC6_MCCONF_VARS];
    const board_vesc6_t *board;
    /* The part's own peripherals, bound by the composition root. Keeping them as pointers is what
     * lets these adapters be exercised against a block of RAM instead of only on a board. */
    soc_stm32f4_tim_regs_t *tim;
    soc_stm32f4_adc_regs_t *adc_current[3];
    soc_stm32f4_adc_regs_t *adc_vbus;
    uint32_t period;   /* the timer's ARR, which the duty fractions are scaled by */
    float deadtime_ns; /* the board's gate-driver dead time, until board/vesc6 owns it */
    /*
     * The phase currents as the interrupt left them: the three ranks each ADC converted, filled by
     * the injected-conversion hook and read by the control loop's port. This is the reference's own
     * shape - its ADC interrupt writes ADC_Value[] and the control path reads that - and it is why
     * the hook exists at all.
     */
    uint16_t current_counts[3][VESC6_CURRENT_RANKS];
} vesc6_glue_state_t;

/* The hook the vector table's ADC entry calls: it reads the finished ADC's ranks into the snapshot
 * above. `adc_index` is 0, 1 or 2 for ADC1, ADC2 and ADC3. */
void vesc6_adc_injected_hook(void *ctx, uint32_t adc_index);

void vesc6_glue_init(vesc6_glue_state_t *self, const board_vesc6_t *board);
void vesc6_make_var_port(motor_config_var_port_t *out, vesc6_glue_state_t *state);
/* The two ports the control loop drives the hardware through, and reads it back from. */
void vesc6_make_inverter_port(foc_inverter_port_t *out, vesc6_glue_state_t *state);
void vesc6_make_current_port(foc_current_port_t *out, vesc6_glue_state_t *state);
/* The angle source. This board's hall driver does not exist yet, so the port says so rather than
 * reporting an angle; a sensorless configuration does not install it at all, because there the
 * observer owns the angle. */
void vesc6_make_rotor_port(foc_rotor_port_t *out, vesc6_glue_state_t *state);

#endif /* PRODUCT_VESC6_STM32F4_GLUE_H */
