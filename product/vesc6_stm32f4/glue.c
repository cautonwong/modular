#include "glue.h"

#include <string.h>

/*
 * Adapter layer (D14): motor_config owns its consumer-defined variable port, infra exposes its
 * own concrete API, and the composition root bridges the two here.
 *
 * The store is a table in RAM. The reference keeps the same words in the EEPROM emulation over
 * flash, and this port is where that goes once the flash driver exists - soc/stm32f4 has no
 * register-level drivers yet (D2), so nothing here may claim to persist. What this port does
 * carry is the part that is not hardware: the read/write protocol, including the failing read
 * for a word that was never written.
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
