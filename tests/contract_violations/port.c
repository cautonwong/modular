/*
 * A contract that cannot fail is documentation with a green tick.
 *
 * This port reports success for a NULL buffer, which is a port that will corrupt
 * caller memory. CTest is told to expect this binary to fail (WILL_FAIL).
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/port_contract.h"
#include "edge/errors.h"

static uint8_t g_state;

static edge_status_t broken_read(void *self, uint32_t key, void *buf, size_t len) {
    (void)self;
    (void)key;
    (void)buf; /* the violation: a NULL buffer is accepted and reports success */
    (void)len;
    return EDGE_OK;
}

static edge_status_t broken_write(void *self, uint32_t key, const void *buf, size_t len) {
    (void)self;
    (void)key;
    (void)buf;
    (void)len;
    return EDGE_OK;
}

static void test_violation_is_rejected(void **state) {
    (void)state;
    const edge_storage_contract_t contract = {
        .name = "violation/null_buffer_accepted",
        .read = broken_read,
        .write = broken_write,
        .self = &g_state,
    };
    edge_contract_storage_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_violation_is_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
