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

void edge_contract_gpio_run(const edge_gpio_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->write && contract->read, name,
                          "read and write callbacks must both be provided");
    edge_contract_require(contract->self != NULL, name,
                          "self must be carried through the port, not dropped");

    bool level = false;
    edge_contract_require(contract->write(contract->self, 1u, true) == EDGE_OK, name,
                          "gpio write high must return EDGE_OK");
    edge_contract_require(contract->read(contract->self, 1u, &level) == EDGE_OK, name,
                          "gpio read must return EDGE_OK");
    edge_contract_require(level == true, name, "read back level must be high after writing high");

    edge_contract_require(contract->write(contract->self, 1u, false) == EDGE_OK, name,
                          "gpio write low must return EDGE_OK");
    edge_contract_require(contract->read(contract->self, 1u, &level) == EDGE_OK, name,
                          "gpio read must return EDGE_OK");
    edge_contract_require(level == false, name, "read back level must be low after writing low");

    /* Boundary verification: NULL pointer arguments must be rejected */
    edge_contract_require(contract->read(contract->self, 1u, NULL) == EDGE_EINVAL, name,
                          "gpio read with NULL level pointer must return EDGE_EINVAL");
    edge_contract_require(contract->write(NULL, 1u, true) == EDGE_EINVAL, name,
                          "gpio write with NULL self must return EDGE_EINVAL");
    edge_contract_require(contract->read(NULL, 1u, &level) == EDGE_EINVAL, name,
                          "gpio read with NULL self must return EDGE_EINVAL");
}

void edge_contract_byte_reader_run(const edge_byte_reader_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->read != NULL, name, "read callback must be provided");
    edge_contract_require(contract->self != NULL, name,
                          "self must be carried through the port, not dropped");

    uint8_t buffer[4] = {0};
    /* Zero length probe */
    edge_contract_require(contract->read(contract->self, buffer, 0u) == EDGE_OK, name,
                          "a zero-length read must return EDGE_OK, not an error");
    /* Boundary verification: NULL pointer arguments must be rejected */
    edge_contract_require(contract->read(contract->self, NULL, sizeof(buffer)) == EDGE_EINVAL, name,
                          "read with a NULL buffer must return EDGE_EINVAL");
    edge_contract_require(contract->read(NULL, buffer, sizeof(buffer)) == EDGE_EINVAL, name,
                          "read with NULL self must return EDGE_EINVAL");
}

void edge_contract_spi_run(const edge_spi_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->transfer != NULL, name, "transfer callback must be provided");
    edge_contract_require(contract->self != NULL, name,
                          "self must be carried through the port, not dropped");

    const uint8_t tx[4] = {0x01, 0x02, 0x03, 0x04};
    uint8_t rx[4] = {0};

    /* Full duplex */
    edge_contract_require(contract->transfer(contract->self, tx, rx, sizeof(tx)) == EDGE_OK, name,
                          "full-duplex transfer must return EDGE_OK");
    /* Transmit only */
    edge_contract_require(contract->transfer(contract->self, tx, NULL, sizeof(tx)) == EDGE_OK, name,
                          "transmit-only transfer must return EDGE_OK");
    /* Receive only */
    edge_contract_require(contract->transfer(contract->self, NULL, rx, sizeof(rx)) == EDGE_OK, name,
                          "receive-only transfer must return EDGE_OK");
    /* Zero length probe */
    edge_contract_require(contract->transfer(contract->self, NULL, NULL, 0u) == EDGE_OK, name,
                          "zero-length transfer must return EDGE_OK");

    /* Boundary rejection: both NULL with nonzero length */
    edge_contract_require(
        contract->transfer(contract->self, NULL, NULL, 4u) == EDGE_EINVAL, name,
        "transfer with both tx and rx NULL for nonzero length must return EDGE_EINVAL");
    /* NULL self */
    edge_contract_require(contract->transfer(NULL, tx, rx, sizeof(tx)) == EDGE_EINVAL, name,
                          "transfer with NULL self must return EDGE_EINVAL");
}

void edge_contract_i2c_run(const edge_i2c_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->write != NULL && contract->read != NULL, name,
                          "both read and write callbacks must be provided");
    edge_contract_require(contract->self != NULL, name,
                          "self must be carried through the port, not dropped");

    const uint16_t dev_addr = 0x50u;
    const uint8_t tx[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t rx[4] = {0};

    /* Valid write and read */
    edge_contract_require(contract->write(contract->self, dev_addr, tx, sizeof(tx)) == EDGE_OK,
                          name, "i2c write must return EDGE_OK");
    edge_contract_require(contract->read(contract->self, dev_addr, rx, sizeof(rx)) == EDGE_OK, name,
                          "i2c read must return EDGE_OK");

    /* Zero length probe */
    edge_contract_require(contract->write(contract->self, dev_addr, tx, 0u) == EDGE_OK, name,
                          "zero-length i2c write must return EDGE_OK");
    edge_contract_require(contract->read(contract->self, dev_addr, rx, 0u) == EDGE_OK, name,
                          "zero-length i2c read must return EDGE_OK");

    /* Boundary rejection */
    edge_contract_require(contract->write(contract->self, dev_addr, NULL, sizeof(tx)) ==
                              EDGE_EINVAL,
                          name, "i2c write with NULL buffer must return EDGE_EINVAL");
    edge_contract_require(contract->read(contract->self, dev_addr, NULL, sizeof(rx)) == EDGE_EINVAL,
                          name, "i2c read with NULL buffer must return EDGE_EINVAL");
    edge_contract_require(contract->write(NULL, dev_addr, tx, sizeof(tx)) == EDGE_EINVAL, name,
                          "i2c write with NULL self must return EDGE_EINVAL");
    edge_contract_require(contract->read(NULL, dev_addr, rx, sizeof(rx)) == EDGE_EINVAL, name,
                          "i2c read with NULL self must return EDGE_EINVAL");
}

void edge_contract_flash_block_run(const edge_flash_block_contract_t *contract) {
    edge_contract_require(contract != NULL, "?", "contract descriptor must not be NULL");
    const char *name = contract->name ? contract->name : "?";
    edge_contract_require(contract->read != NULL && contract->write != NULL &&
                              contract->erase != NULL,
                          name, "read, write, and erase callbacks must all be provided");
    edge_contract_require(contract->self != NULL, name,
                          "self must be carried through the port, not dropped");

    const uint32_t offset = 0x1000u;
    const uint8_t tx[16] = {0x12, 0x34, 0x56, 0x78};
    uint8_t rx[16] = {0};

    edge_contract_require(contract->erase(contract->self, offset, 4096u) == EDGE_OK, name,
                          "flash erase must return EDGE_OK");
    edge_contract_require(contract->write(contract->self, offset, tx, sizeof(tx)) == EDGE_OK, name,
                          "flash write must return EDGE_OK");
    edge_contract_require(contract->read(contract->self, offset, rx, sizeof(rx)) == EDGE_OK, name,
                          "flash read must return EDGE_OK");

    /* Zero length probe */
    edge_contract_require(contract->erase(contract->self, offset, 0u) == EDGE_OK, name,
                          "zero-length flash erase must return EDGE_OK");
    edge_contract_require(contract->write(contract->self, offset, tx, 0u) == EDGE_OK, name,
                          "zero-length flash write must return EDGE_OK");
    edge_contract_require(contract->read(contract->self, offset, rx, 0u) == EDGE_OK, name,
                          "zero-length flash read must return EDGE_OK");

    /* Boundary rejection */
    edge_contract_require(contract->write(contract->self, offset, NULL, sizeof(tx)) == EDGE_EINVAL,
                          name, "flash write with NULL buffer must return EDGE_EINVAL");
    edge_contract_require(contract->read(contract->self, offset, NULL, sizeof(rx)) == EDGE_EINVAL,
                          name, "flash read with NULL buffer must return EDGE_EINVAL");
    edge_contract_require(contract->write(NULL, offset, tx, sizeof(tx)) == EDGE_EINVAL, name,
                          "flash write with NULL self must return EDGE_EINVAL");
    edge_contract_require(contract->read(NULL, offset, rx, sizeof(rx)) == EDGE_EINVAL, name,
                          "flash read with NULL self must return EDGE_EINVAL");
    edge_contract_require(contract->erase(NULL, offset, 4096u) == EDGE_EINVAL, name,
                          "flash erase with NULL self must return EDGE_EINVAL");
}
