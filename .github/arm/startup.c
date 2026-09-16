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

typedef void (*edge_isr_handler_t)(void);

typedef struct {
    uint32_t initial_sp;
    edge_isr_handler_t reset;
    edge_isr_handler_t handlers[46];
} edge_vector_table_t;

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
#define EDGE_IRQ_HANDLER(irq) board_example_irq_timer0
#else
#define EDGE_IRQ_HANDLER(irq) default_handler
#endif

// clang-format off
__attribute__((used, section(".isr_vector"))) const edge_vector_table_t edge_vector_table = {
    .initial_sp = (uint32_t)&_estack,
    .reset = _start,
    .handlers = {
        [0] = default_handler,
        [1] = default_handler,
        [2] = default_handler,
        [3] = default_handler,
        [4] = default_handler,
        [5] = default_handler,
        [6] = default_handler,
        [7] = default_handler,
        [8] = default_handler,
        [9] = default_handler,
        [10] = default_handler,
        [11] = default_handler,
        [12] = default_handler,
        [13] = default_handler,
        [14] = default_handler,
        [15] = default_handler,
        [16] = default_handler,
        [17] = default_handler,
        [18] = default_handler,
        [19] = default_handler,
        [20] = default_handler,
        [21] = default_handler,
        [22] = default_handler,
        [23] = EDGE_IRQ_HANDLER(8),
        [24] = default_handler,
        [25] = default_handler,
        [26] = default_handler,
        [27] = default_handler,
        [28] = default_handler,
        [29] = default_handler,
        [30] = default_handler,
        [31] = default_handler,
        [32] = default_handler,
        [33] = default_handler,
        [34] = default_handler,
        [35] = default_handler,
        [36] = default_handler,
        [37] = default_handler,
        [38] = default_handler,
        [39] = default_handler,
        [40] = default_handler,
        [41] = default_handler,
        [42] = default_handler,
        [43] = default_handler,
        [44] = default_handler,
        [45] = default_handler,
    },
};
// clang-format on

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
