/*
 * A contract that cannot fail is documentation with a green tick.
 *
 * This module breaks one promise on purpose: `power_off` is missing, which `sys`
 * calls at shutdown. CTest is told to expect this binary to fail (WILL_FAIL), so
 * if the app contract ever stops rejecting the violation, the binary passes and
 * CTest reports that instead.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/app_contract.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"

static edge_module_t g_module;

static edge_status_t broken_poll(edge_module_t *self) {
    (void)self;
    return EDGE_OK;
}

static edge_status_t broken_event(edge_module_t *self, const edge_event_t *event) {
    (void)self;
    return event == NULL ? EDGE_EINVAL : EDGE_OK;
}

static edge_status_t prepare(void) {
    g_module.module_id = 0x1500u;
    g_module.priority = 50u;
    g_module.poll = broken_poll;
    g_module.on_event = broken_event;
    g_module.power_off = NULL; /* the violation */
    g_module.private_data = &g_module;
    return EDGE_OK;
}

static edge_status_t release(void) {
    return EDGE_OK;
}

static const edge_module_t *module(void) {
    return &g_module;
}

static void test_violation_is_rejected(void **state) {
    (void)state;
    const edge_app_contract_t contract = {
        .name = "violation/missing_power_off",
        .prepare = prepare,
        .release = release,
        .module = module,
        .module_id = 0x1500u,
        .priority = 50u,
    };
    edge_contract_app_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_violation_is_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
