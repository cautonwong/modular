#include <stdint.h>

void uart_rx_isr(void) {
    uint8_t b = 0xAA;
    (void)b;
}
