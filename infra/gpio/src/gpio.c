#include "gpio/gpio.h"

/* Host-testable fake: `self` points at a byte-per-channel state buffer. */
edge_status_t gpio_write(void *self, uint8_t channel, bool level) {
    if (self == NULL)
        return EDGE_EINVAL;
    ((uint8_t *)self)[channel] = level ? 1u : 0u;
    return EDGE_OK;
}
