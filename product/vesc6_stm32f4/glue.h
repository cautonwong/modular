#ifndef PRODUCT_VESC6_STM32F4_GLUE_H
#define PRODUCT_VESC6_STM32F4_GLUE_H

#include "edge/errors.h"
#include "motor_config/motor_config.h"
#include "vesc6/board.h"

#include <stdint.h>

/*
 * The reference keeps its configuration in the EEPROM emulation, one u16 per configuration word,
 * starting at its own virtual base (conf_general.c:50 EEPROM_BASE_MCCONF = 1000, table built at
 * conf_general.c:72). That many words is what a store has to hold; the count is the size of the
 * reference's variable list, and it is the reason motor_config writes as many entries as it does.
 */
#define VESC6_MCCONF_VARS 388u

/*
 * The composition root's state (D1). None of it is hardware: the store below is a table in RAM,
 * and the board scales come from board/vesc6 rather than being re-derived here.
 *
 * Every entry carries a written flag because a stored word and an unstored one are not the same
 * value: the reference's EEPROM read fails for a word that was never written, and the
 * configuration relies on that failure to tell "no stored configuration" from "a configuration
 * of zeroes" - which is a real configuration, and its CRC is zero (see motor_config.h:42-46).
 */
typedef struct vesc6_glue_state {
    uint16_t values[VESC6_MCCONF_VARS];
    uint8_t written[VESC6_MCCONF_VARS];
    const board_vesc6_t *board;
} vesc6_glue_state_t;

void vesc6_glue_init(vesc6_glue_state_t *self, const board_vesc6_t *board);
void vesc6_make_var_port(motor_config_var_port_t *out, vesc6_glue_state_t *state);

#endif /* PRODUCT_VESC6_STM32F4_GLUE_H */
