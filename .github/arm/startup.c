#include <stdint.h>

extern int main(void);
extern void _start(void);
extern uint8_t _estack;
extern uint8_t _sidata;
extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _sbss;
extern uint8_t _ebss;

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
    /*
     * SYS_EXIT_EXTENDED (0x20), not SYS_EXIT: r1 points at
     * { ADP_Stopped_ApplicationExit, status }, so `main()`'s return value reaches
     * the harness. Plain SYS_EXIT collapses every non-zero status to 1, which is
     * exactly the distinction a smoke test needs (see board_mps2_exit, which took
     * the same fix).
     */
    const uint32_t args[2] = {0x20026u, (uint32_t)status};
    register int r0 __asm("r0") = 0x20; /* SYS_EXIT_EXTENDED */
    register const uint32_t *r1 __asm("r1") = args;
    __asm volatile("bkpt 0xAB" : : "r"(r0), "r"(r1) : "memory");
    default_handler();
}
#endif

#if defined(EDGE_BOARD_TIMER_ISR)
extern void EDGE_BOARD_TIMER_ISR(void);
#define EDGE_TIMER_HANDLER EDGE_BOARD_TIMER_ISR
#else
#define EDGE_TIMER_HANDLER default_handler
#endif

#if defined(EDGE_SVC_HANDLER)
extern void EDGE_SVC_HANDLER(void);
#define EDGE_VECT_SVC EDGE_SVC_HANDLER
#else
#define EDGE_VECT_SVC default_handler
#endif

#if defined(EDGE_PENDSV_HANDLER)
extern void EDGE_PENDSV_HANDLER(void);
#define EDGE_VECT_PENDSV EDGE_PENDSV_HANDLER
#else
#define EDGE_VECT_PENDSV default_handler
#endif

#if defined(EDGE_SYSTICK_HANDLER)
extern void EDGE_SYSTICK_HANDLER(void);
#define EDGE_VECT_SYSTICK EDGE_SYSTICK_HANDLER
#else
#define EDGE_VECT_SYSTICK default_handler
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
        [9] = EDGE_VECT_SVC,
        [10] = default_handler,
        [11] = default_handler,
        [12] = EDGE_VECT_PENDSV,
        [13] = EDGE_VECT_SYSTICK,
        [14] = default_handler,
        [15] = default_handler,
        [16] = default_handler,
        [17] = default_handler,
        [18] = default_handler,
        [19] = default_handler,
        [20] = default_handler,
        [21] = default_handler,
        [22] = EDGE_TIMER_HANDLER,
        [23] = default_handler,
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
