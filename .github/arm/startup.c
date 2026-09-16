#include <stdint.h>

extern int main(void);
extern void _start(void);
extern uint8_t _estack;
extern uint8_t _sidata;
extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _sbss;
extern uint8_t _ebss;

#ifdef EDGE_QEMU_SEMIHOSTING
extern void board_example_irq_timer0(void);
#endif

__attribute__((noreturn)) static void default_handler(void) {
    for (;;) {
        __asm volatile("wfi");
    }
}

#ifdef EDGE_QEMU_SEMIHOSTING
__attribute__((noreturn)) static void qemu_exit(int status) {
    register int r0 __asm("r0") = 0x18; /* SYS_EXIT */
    register int r1 __asm("r1") = status == 0 ? 0x20026 : 0x20023;
    __asm volatile("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
    default_handler();
}
#endif

#ifdef EDGE_QEMU_SEMIHOSTING
#define EDGE_IRQ_HANDLER(irq) ((uintptr_t)(board_example_irq_timer0) | 1u)
#else
#define EDGE_IRQ_HANDLER(irq) ((uintptr_t)default_handler)
#endif

__attribute__((used, section(".isr_vector"))) const uintptr_t edge_vector_table[48] = {
    (uintptr_t)&_estack,
    (uintptr_t)&_start + 1u,
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
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    (uintptr_t)default_handler,
    EDGE_IRQ_HANDLER(8),
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

void _start(void) {
    uint8_t *src = &_sidata;
    uint8_t *dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0u;
    }

    const int status = main();
#ifdef EDGE_QEMU_SEMIHOSTING
    qemu_exit(status);
#else
    (void)status;
    default_handler();
#endif
}
