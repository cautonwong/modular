#ifndef EDGE_TEST_PORT_CONTRACT_H
#define EDGE_TEST_PORT_CONTRACT_H

#include "edge/errors.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Port contract (D14/D22/D23/D24): a narrow port declares a *shape*, and a shape
 * says nothing about behaviour -- what a zero-length write means, whether a NULL
 * buffer is rejected or dereferenced, whether `self` survives the trampoline.
 * Those answers are written once here and any implementation is run against them:
 * a product's glue trampoline, a driver behind it, or a test fake.
 *
 * The callbacks are passed member by member rather than as `edge_storage_kv_t`,
 * because an app defines its own port type (D14). The shapes agree; the C types
 * deliberately do not, which is exactly why the glue exists.
 */
typedef struct edge_storage_contract {
    const char *name;
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
    void *self;
} edge_storage_contract_t;

void edge_contract_storage_run(const edge_storage_contract_t *contract);

typedef struct edge_byte_writer_contract {
    const char *name;
    edge_status_t (*write)(void *self, const void *buf, size_t len);
    void *self;
} edge_byte_writer_contract_t;

void edge_contract_byte_writer_run(const edge_byte_writer_contract_t *contract);

#endif
