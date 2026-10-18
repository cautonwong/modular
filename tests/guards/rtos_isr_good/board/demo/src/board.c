#include <stdint.h>

void board_demo_irq_timer0(void) {
    /* Safe minimal capture and flag clear */
    volatile uint32_t *flag = (volatile uint32_t *)0x40000000;
    *flag = 0;
}
