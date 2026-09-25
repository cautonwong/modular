#ifndef INFRA_FLASH_H
#define INFRA_FLASH_H

#include "edge/module.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

edge_status_t flash_read(const void *self, uint32_t key, void *buf, size_t len);
edge_status_t flash_write(void *self, uint32_t key, const void *buf, size_t len);

/*
 * EEPROM emulation over flash, the reference's driver/eeprom.c: two whole pages (one flash
 * sector each), one valid and one receiving, records appended as [data][virt_address] with an
 * erased 32-bit word marking the end, and a page transfer when the receiving page fills. A
 * sector can only be erased whole, which is why the old page stays intact until the new one is
 * complete. Values are little-endian, as they are in the reference's memory image (the
 * reference uses absolute STM32 addresses; here the sector backend is a port so the same
 * semantics run against a mock).
 *
 * Fidelity note worth keeping: the reference erases the old page *before* marking the new one
 * valid, so there is a narrow window where a power loss leaves no valid page. That is the
 * reference's behaviour, not an oversight here, and the tests pin the recoverable cases around
 * it rather than pretending the window does not exist.
 */
#define FLASH_EMUL_PAGE_SIZE 0x4000u           /* driver/eeprom.h: PAGE_SIZE, one sector */
#define FLASH_EMUL_PAGE_MARKER_ERASED 0xFFFFu  /* driver/eeprom.h:61 */
#define FLASH_EMUL_PAGE_MARKER_RECEIVE 0xEEEEu /* :62 */
#define FLASH_EMUL_PAGE_MARKER_VALID 0x0000u   /* :63 */
#define FLASH_EMUL_NO_VALID_PAGE 0x00ABu       /* driver/eeprom.h: NO_VALID_PAGE */

/* Which page to work on, mirroring the reference's READ_FROM_VALID_PAGE /
 * WRITE_IN_VALID_PAGE operations (driver/eeprom.h:66-67). */
typedef enum {
    FLASH_EMUL_OP_READ = 0u,
    FLASH_EMUL_OP_WRITE = 1u,
} flash_emul_op_t;

typedef struct flash_sector_port {
    edge_status_t (*read)(void *self, uint32_t offset, uint8_t *buf, size_t len);
    /* Flash programs half-words, and only into space that is still erased. */
    edge_status_t (*write_halfword)(void *self, uint32_t offset, uint16_t value);
    /* Erasure is whole-sector: that granularity is what the two-page dance works around. */
    edge_status_t (*erase_sector)(void *self, uint32_t offset, size_t len);
    void *self;
} flash_sector_port_t;

/*
 * The consumer's variable table: the reference's VirtAddVarTab. The transfer walks it, which is
 * why it has to be given rather than discovered - a page can only be rebuilt from a known key
 * list, since the records themselves are just (data, key) pairs.
 */
typedef struct flash_var_table {
    const uint16_t *virtual_addresses;
    size_t count;
} flash_var_table_t;

typedef struct flash_emul {
    const flash_sector_port_t *port;
    flash_var_table_t table;
    uint32_t base_offset;
} flash_emul_t;

void flash_emul_construct(flash_emul_t *self, const flash_sector_port_t *port,
                          flash_var_table_t table, uint32_t base_offset);

/* Finds the valid page, formatting both and marking page 0 valid if there is none (reference
 * EE_Init / EE_Format); writes then append into that valid page until a transfer starts. */
edge_status_t flash_emul_init(flash_emul_t *self);
edge_status_t flash_emul_read(flash_emul_t *self, uint16_t virtual_address, uint16_t *out);
edge_status_t flash_emul_write(flash_emul_t *self, uint16_t virtual_address, uint16_t data);

/* The page the last successful operation used, for tests and diagnostics: FLASH_EMUL_PAGE_SIZE
 * apart, so a test can assert which one is live. */
uint32_t flash_emul_valid_page_offset(const flash_emul_t *self);

#ifdef __cplusplus
}
#endif

#endif
