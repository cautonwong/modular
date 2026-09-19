#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/app_contract.h"

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/events.h"

/*
 * An event id in another module's segment. Every app must ignore what it does not
 * own: a module that reacts to a foreign fact is a module that will misbehave
 * once a second app exists.
 */
#define EDGE_CONTRACT_FOREIGN_EVENT_ID (EDGE_EVT_BOARD_BASE + 0x7Fu)

/* Every check reports which contract item it is: a bare assert tells the author
 * that something failed, a named failure tells them what to fix. */
static void edge_contract_require(bool condition, const char *name, const char *item) {
    if (!condition) {
        fail_msg("[%s] app contract violated: %s", name, item);
    }
}

void edge_contract_app_run(const edge_app_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->prepare && contract->release && contract->module, name,
                          "prepare/release/module callbacks must all be provided");

    /* Assembly-time init is the composition root's contract with the module
     * (D51); it must succeed on a healthy module with valid dependencies. */
    edge_contract_require(contract->prepare() == EDGE_OK, name,
                          "prepare() (construct + init) must return EDGE_OK");

    const edge_module_t *module = contract->module();
    edge_contract_require(module != NULL, name, "module() must return the module");

    /* Identity and scheduling come from the construct arguments (D5). */
    edge_contract_require(module->module_id == contract->module_id, name,
                          "module_id must equal the id passed to construct");
    edge_contract_require(module->priority == contract->priority, name,
                          "priority must equal the priority passed to construct");

    /* sys calls these every run and at shutdown; a NULL would fault in the field
     * rather than here. */
    edge_contract_require(module->poll != NULL, name, "poll must be set (sys calls it)");
    edge_contract_require(module->on_event != NULL, name, "on_event must be set");
    edge_contract_require(module->power_off != NULL, name,
                          "power_off must be set (sys orders shutdown through it)");

    /* The instance must be reachable from the module, nobody reaches into the
     * struct directly (D14). */
    edge_contract_require(edge_module_data((edge_module_t *)module) != NULL, name,
                          "private_data must point at the module instance");

    edge_module_t *subject = (edge_module_t *)module;
    const edge_event_t foreign = {
        .id = EDGE_CONTRACT_FOREIGN_EVENT_ID,
        .source = 0u,
        .arg0 = 0u,
        .arg1 = 0u,
        .timestamp = 0u,
    };
    edge_contract_require(subject->on_event(subject, &foreign) == EDGE_OK, name,
                          "a foreign event must be ignored, not reported as a failure");
    edge_contract_require(subject->on_event(subject, NULL) == EDGE_EINVAL, name,
                          "a NULL event must be rejected with EDGE_EINVAL");

    /* Shutdown is the composition root's call as well, in reverse order. */
    edge_contract_require(contract->release() == EDGE_OK, name, "release() must return EDGE_OK");
}
