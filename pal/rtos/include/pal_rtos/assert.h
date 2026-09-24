#ifndef PAL_RTOS_ASSERT_H
#define PAL_RTOS_ASSERT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RTOS assert contract (D87), deliberately dependency-free.
 *
 * The product-owned `FreeRTOSConfig.h` maps `configASSERT` onto
 * `edge_rtos_assert_failed()`. That header is included by every FreeRTOS kernel
 * translation unit, so this contract must not pull in the framework or the OS
 * port headers -- it declares only the symbols the configuration needs.
 *
 * A failed assert is never silent: it counts and halts. The composition root
 * installs an observable reaction (e.g. a semihosting exit with a distinct code)
 * through `edge_rtos_set_assert_hook`.
 */
typedef void (*edge_rtos_assert_fn)(void *ctx, const char *file, int line);

void edge_rtos_assert_failed(const char *file, int line);
void edge_rtos_fault_handler(void);
void edge_rtos_set_assert_hook(edge_rtos_assert_fn fn, void *ctx);
uint32_t edge_rtos_assert_count(void);

#ifdef __cplusplus
}
#endif

#endif
