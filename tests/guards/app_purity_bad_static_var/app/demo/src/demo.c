#include <stdint.h>

/* VIOLATION: non-const file-scope static variable in app */
static uint32_t g_hidden_counter = 0u;

void demo_step(void) {
    g_hidden_counter++;
}
