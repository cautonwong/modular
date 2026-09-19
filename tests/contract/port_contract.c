#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>
#include <string.h>

#include "contract/port_contract.h"

static void edge_contract_require(bool condition, const char *name, const char *item) {
    if (!condition) {
        fail_msg("[%s] port contract violated: %s", name, item);
    }
}

void edge_contract_storage_run(const edge_storage_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->read && contract->write, name,
                          "read and write callbacks must both be provided");
    edge_contract_require(contract->self != NULL, name,
                          "self must be carried through the port, not dropped");

    uint8_t store[32] = {0};
    const uint8_t input[4] = {0x11u, 0x22u, 0x33u, 0x44u};
    uint8_t output[4] = {0};
    (void)store;

    edge_contract_require(contract->write(contract->self, 0u, input, sizeof(input)) == EDGE_OK,
                          name, "write of a valid buffer must return EDGE_OK");
    edge_contract_require(contract->read(contract->self, 0u, output, sizeof(output)) == EDGE_OK,
                          name, "read of a valid buffer must return EDGE_OK");
    edge_contract_require(memcmp(input, output, sizeof(input)) == 0, name,
                          "a value read back must equal the value written");

    /* Zero length is a no-op, not an error: callers use it to probe. */
    edge_contract_require(contract->write(contract->self, 0u, input, 0u) == EDGE_OK, name,
                          "a zero-length write must return EDGE_OK, not an error");
    edge_contract_require(contract->read(contract->self, 0u, output, 0u) == EDGE_OK, name,
                          "a zero-length read must return EDGE_OK, not an error");

    /* Invalid arguments are rejected, never dereferenced. */
    edge_contract_require(contract->read(contract->self, 0u, NULL, sizeof(output)) == EDGE_EINVAL,
                          name, "read with a NULL buffer must return EDGE_EINVAL");
    edge_contract_require(contract->write(contract->self, 0u, NULL, sizeof(input)) == EDGE_EINVAL,
                          name, "write with a NULL buffer must return EDGE_EINVAL");
}

void edge_contract_byte_writer_run(const edge_byte_writer_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->write != NULL, name, "write callback must be provided");
    edge_contract_require(contract->self != NULL, name,
                          "self must be carried through the port, not dropped");

    const uint8_t payload[3] = {0xA1u, 0xB2u, 0xC3u};

    edge_contract_require(contract->write(contract->self, payload, sizeof(payload)) == EDGE_OK,
                          name, "write of a valid buffer must return EDGE_OK");
    edge_contract_require(contract->write(contract->self, payload, 0u) == EDGE_OK, name,
                          "a zero-length write must return EDGE_OK, not an error");
    edge_contract_require(contract->write(contract->self, NULL, sizeof(payload)) == EDGE_EINVAL,
                          name, "write with a NULL buffer must return EDGE_EINVAL");
}
