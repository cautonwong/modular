/* Negative fixture: an app reaching a raw peripheral register (N3). */
#include "edge/module.h"

#define ALPHA_REG (*(volatile uint32_t *)0x40000000u)

void alpha_touch(void) {
    ALPHA_REG = 1u;
}
