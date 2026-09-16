#ifndef INFRA_GPIO_H
#define INFRA_GPIO_H

#include "edge/module.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Concrete infrastructure API; products adapt it to consumer-defined ports. */
edge_status_t gpio_write(void *self, uint8_t channel, bool level);

#ifdef __cplusplus
}
#endif

#endif
