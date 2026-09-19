/*
 * Contract adoption against real implementations.
 *
 * Every product's glue storage trampoline, the gateway transport trampoline, and
 * the host board's IRQ entry point are run against the reusable suites. A module
 * adopts a suite in a few lines -- that is the point, and the app tests show the
 * same pattern in a module's own test file.
 *
 * The proofs that these suites can *fail* live in tests/contract_violations/,
 * where CTest is told to expect the failure (WILL_FAIL). A contract that cannot
 * fail is documentation with a green tick.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "contract/board_contract.h"
#include "contract/port_contract.h"
#include "dlt645/dlt645.h"
#include "edge/events.h"
#include "example/board.h"
#include "gateway.h"
#include "modbus_slave/modbus_slave.h"

/* The product glue factories are the composition root's public surface. */
void product_example_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_mps2_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_gateway_host_make_storage(dlt645_storage_if_t *out, void *flash_state);
void product_meter_gateway_host_make_modbus(modbus_store_if_t *store,
                                            modbus_transport_if_t *transport,
                                            gateway_state_t *state);

typedef void (*make_storage_fn)(dlt645_storage_if_t *, void *);

static void check_storage_adapter(const char *name, make_storage_fn make) {
    uint8_t flash[32] = {0};
    dlt645_storage_if_t port = {0};
    make(&port, flash);
    const edge_storage_contract_t contract = {
        .name = name,
        .read = port.read,
        .write = port.write,
        .self = port.self,
    };
    edge_contract_storage_run(&contract);
}

static void test_every_product_storage_adapter(void **state) {
    (void)state;
    check_storage_adapter("example/storage", product_example_make_storage);
    check_storage_adapter("meter_host/storage", product_meter_host_make_storage);
    check_storage_adapter("meter_mps2/storage", product_meter_mps2_make_storage);
    check_storage_adapter("meter_gateway_host/storage", product_meter_gateway_host_make_storage);
}

static void test_gateway_transport_adapter(void **state) {
    (void)state;
    gateway_state_t gateway = {0};
    modbus_store_if_t store = {0};
    modbus_transport_if_t transport = {0};
    product_meter_gateway_host_make_modbus(&store, &transport, &gateway);

    const edge_byte_writer_contract_t contract = {
        .name = "meter_gateway_host/transport",
        .write = transport.write,
        .self = transport.self,
    };
    edge_contract_byte_writer_run(&contract);
}

static void test_host_board_irq_entry_point(void **state) {
    (void)state;
    const edge_board_contract_t contract = {
        .name = "board/example uart0_rx",
        .init = board_example_init,
        .irq = board_example_irq_uart0_rx,
        .expected_event_id = EDGE_EVT_UART0_RX,
    };
    edge_contract_board_run(&contract);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_every_product_storage_adapter),
        cmocka_unit_test(test_gateway_transport_adapter),
        cmocka_unit_test(test_host_board_irq_entry_point),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
