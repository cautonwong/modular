#ifndef EDGE_TEST_APP_CONTRACT_H
#define EDGE_TEST_APP_CONTRACT_H

#include "edge/module.h"

/*
 * App contract (D5/D14/D51): what every application module promises, written once
 * and executed against a real module, so a new app proves itself instead of
 * relying on a reviewer to notice. `sys` relies on all of it in the field:
 * `power_off` is what it calls at shutdown, and `poll`/`on_event` are what it
 * calls every run.
 *
 * Adoption is four lines in the module's own test file (see
 * `docs/how-to/add-app.md`, "Proving the contract"): a `prepare()` that
 * constructs and inits, a `release()` that deinits, and `module()`, `module_id`,
 * `priority`.
 */
typedef struct edge_app_contract {
    const char *name;                     /* appears in failure output */
    edge_status_t (*prepare)(void);       /* construct + init; returns the init status */
    edge_status_t (*release)(void);       /* deinit */
    const edge_module_t *(*module)(void); /* <app>_module(self) once prepared */
    uint32_t module_id;                   /* the id handed to construct */
    uint32_t priority;                    /* the priority handed to construct */
} edge_app_contract_t;

void edge_contract_app_run(const edge_app_contract_t *contract);

#endif
