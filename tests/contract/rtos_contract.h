#ifndef EDGE_TEST_RTOS_CONTRACT_H
#define EDGE_TEST_RTOS_CONTRACT_H

#include "edge/errors.h"
#include "pal_os/os.h"
#include "pal_rtos/rtos.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * RTOS port contract (D46/D47/D85).
 *
 * The contract in `pal_rtos/rtos.h` is a set of free functions, one port per
 * product, so what a suite needs is the same shape the other contract suites use: a
 * struct of the port's entry points, passed in. A port supplies its own functions
 * (EOS does, on the host) and the suite is identical for all of them.
 *
 * What this suite cannot check by itself, and how the gap is closed: anything that
 * requires the scheduler to run. `edge_rtos_start()` does not return by contract and
 * a port may not add a "step" entry point to the product contract for a test's
 * benefit. So the *harness* may supply one (`step`, below); when it does, the
 * priority direction becomes observable and the suite asserts it. A port without a
 * harness step proves the direction the way it proves everything else on target:
 * through its product's behaviour test (the runner-starvation probe).
 */
typedef struct edge_rtos_contract {
    const char *name;
    /* The first priority value this port cannot represent; the suite requires it to
     * be rejected. 0 would be nonsense, so a port that cannot state a maximum should
     * not be run through the suite. */
    uint32_t max_priority;
    edge_status_t (*task_create)(const char *name, edge_rtos_task_fn fn, void *arg,
                                 uint32_t stack_words, uint32_t priority);
    void (*start)(void);
    edge_os_port_t (*os_port)(void);
    uint32_t (*task_stack_high_water)(void);
    void (*wake_target_set_self)(void);
    void (*wake_from_isr)(void);
    bool (*wait_for_work)(uint32_t timeout_ticks);
    /* Test-harness hook, deliberately outside the product contract: advance the
     * scheduler by one bounded step. NULL means "this port cannot be stepped", and
     * the suite then checks only what it can observe without running. */
    void (*step)(void *ctx);
    void *step_ctx;
} edge_rtos_contract_t;

void edge_contract_rtos_run(const edge_rtos_contract_t *contract);

#endif
