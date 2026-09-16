#include <stdint.h>

extern int main(void);
extern void _start(void);
extern uint8_t _estack;
extern uint8_t _sidata;
extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _sbss;
extern uint8_t _ebss;
extern uint8_t _start_thumb;

__attribute__((noreturn)) static void default_handler(void) {
    for (;;) {
        __asm volatile("wfi");
    }
}

#ifdef EDGE_QEMU_SEMIHOSTING
__attribute__((noreturn)) static void qemu_exit(int status) {
    register int r0 __asm("r0") = 0x18; /* SYS_EXIT */
    register int r1 __asm("r1") = status;
    __asm volatile("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
    default_handler();
}
#endif

__attribute__((used, section(".isr_vector"))) const uintptr_t edge_vector_table[16] = {
    (uintptr_t)&_estack,        (uintptr_t)&_start_thumb,   (uintptr_t)default_handler,
    (uintptr_t)default_handler, (uintptr_t)default_handler, (uintptr_t)default_handler,
    (uintptr_t)default_handler, (uintptr_t)default_handler, (uintptr_t)default_handler,
    (uintptr_t)default_handler, (uintptr_t)default_handler, (uintptr_t)default_handler,
    (uintptr_t)default_handler, (uintptr_t)default_handler, (uintptr_t)default_handler,
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
