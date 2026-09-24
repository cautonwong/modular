#ifndef EDGE_THREADX_CONFIG_H
#define EDGE_THREADX_CONFIG_H

/*
 * PAL-side ThreadX configuration contract (D87), issue #134.
 *
 * The product owns `tx_user.h` and includes this file **last**, exactly as the
 * FreeRTOS side includes `edge_rtos_config.h`: this file fills in the defaults a
 * product is not expected to change and fails the build when a product disabled
 * something the PAL depends on.
 *
 * It is **pure preprocessor on purpose**: ThreadX includes `tx_user.h` from
 * assembly as well as C (`TX_INCLUDE_USER_DEFINE_FILE`), so no typedef, no include
 * and no C declaration may appear here.
 */

/*
 * Required invariants.
 *
 * ThreadX has no assert macro - it reports bad parameters with return codes and
 * offers TX_DISABLE_ERROR_CHECKING to stop checking them at all. The closest thing
 * to "a failed assert is never silent" is therefore: never disable the checks, and
 * always enable the stack checking that feeds the fault hook.
 */
#ifdef TX_DISABLE_ERROR_CHECKING
#error "PAL requires ThreadX error checking: TX_DISABLE_ERROR_CHECKING must not be defined"
#endif

#ifndef TX_ENABLE_STACK_CHECKING
#error "PAL requires TX_ENABLE_STACK_CHECKING for the stack-overrun path"
#endif

#ifdef TX_DISABLE_STACK_FILLING
#error                                                                                             \
    "PAL requires TX_DISABLE_STACK_FILLING to be undefined; stack checking needs the fill pattern"
#endif

/*
 * The mask mode must be a decision, not a default. PRIMASK masks every interrupt
 * (including ones above the syscall ceiling); BASEPRI keeps the priority ceiling the
 * other PALs use. Either is defensible, silence is not.
 */
#ifndef EDGE_THREADX_MASK_MODE
#error "PAL requires EDGE_THREADX_MASK_MODE to be stated by the product: 'primask' or 'basepri'"
#endif

/* The masking itself comes from TX_PORT_USE_BASEPRI and TX_PORT_BASEPRI in tx_port.h,
 * which the product must define for C *and* assembly. Stating the mode here is only
 * worth anything if it is checked, so the two are tied together. */
#if defined(EDGE_THREADX_MASK_MODE) && (EDGE_THREADX_MASK_MODE == 1)
#if !defined(TX_PORT_USE_BASEPRI)
// cppcheck-suppress preprocessorErrorDirective ; tx_port.h defines it for both C and
// assembly; cppcheck cannot see the vendor header, so it reaches this branch blind.
#error "EDGE_THREADX_MASK_MODE says basepri, but TX_PORT_USE_BASEPRI is not defined"
#endif
#endif

/*
 * Tick frequency stated explicitly: it is the unit of `edge_rtos_wait_for_work()`'s
 * timeout and of `period`/`budget` when a kernel-tick clock is injected
 * (docs/time-model.md).
 */
#ifndef TX_TIMER_TICKS_PER_SECOND
#error "PAL requires TX_TIMER_TICKS_PER_SECOND to be stated explicitly"
#endif

/*
 * Defaults for values a product is not expected to change.
 */
#ifndef TX_MAX_PRIORITIES
#define TX_MAX_PRIORITIES 32
#endif

#endif
