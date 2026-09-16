#include <stdint.h>

extern int main(void);

__attribute__((used, section(".isr_vector")))
const uintptr_t edge_vector_table[] = {
    0x20004000u,
    (uintptr_t)main,
};

void _start(void)
{
    (void)main();
    for (;;) {
        __asm volatile ("wfi");
    }
}
