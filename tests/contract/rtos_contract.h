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
 * What this suite does *not* check, and why: anything that requires the scheduler to
 * run. `edge_rtos_start()` does not return by contract, a fixed-rate executive and a
 * preemptive kernel cannot be stepped the same way, and inventing a "step" entry
 * point would put a test-only primitive into the product contract. Priority
 * *direction* is therefore evidenced elsewhere and by construction: EOS's own test
 * asserts that priority 0 runs first, and a kernel port proves it through its
 * product's behaviour test (the runner-starvation probe).
 */
typedef struct edge_rtos_contract {
    const char *name;
    edge_status_t (*task_create)(const char *name, edge_rtos_task_fn fn, void *arg,
                                 uint32_t stack_words, uint32_t priority);
    void (*start)(void);
    edge_os_port_t (*os_port)(void);
    uint32_t (*task_stack_high_water)(void);
    void (*wake_target_set_self)(void);
    void (*wake_from_isr)(void);
    bool (*wait_for_work)(uint32_t timeout_ticks);
} edge_rtos_contract_t;

void edge_contract_rtos_run(const edge_rtos_contract_t *contract);

#endif
