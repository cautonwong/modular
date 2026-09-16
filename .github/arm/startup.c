#include <stdint.h>

extern int main(void);
extern void _start(void);
extern uint8_t _estack;
extern uint8_t _sidata;
extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _sbss;
extern uint8_t _ebss;

__attribute__((noreturn)) static void default_handler(void)
{
    for (;;) {
        __asm volatile ("wfi");
    }
}

__attribute__((used, section(".isr_vector")))
const uintptr_t edge_vector_table[16] = {
    (uintptr_t)&_estack,
    (uintptr_t)_start | 1u,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
};

void _start(void)
{
    uint8_t *src = &_sidata;
    uint8_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }

    (void)main();
    default_handler();
}
