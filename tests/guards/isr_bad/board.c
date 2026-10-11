#include <stdlib.h>

void bad_timer_irq(void) {
    void *p = malloc(16);
    (void)p;
}
