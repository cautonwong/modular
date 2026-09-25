/*
 * The reference's EEPROM emulation (vendor/bldc/driver/eeprom.c) with this port's
 * conventions: the sector backend is injected rather than writing to absolute STM32
 * addresses, and the variable table comes from the consumer. The structure, the page
 * markers, the record layout, the scan directions and the order of the page transfer are the
 * reference's; each routine names the one it mirrors.
 *
 * A record is four bytes: the data half-word first, then the virtual address. An erased
 * 32-bit word marks the end of the used area, and the page's own first half-word is its
 * status, so the first record slot is at base + 4 even on an empty page.
 */
#include "flash/flash.h"
#include <stdbool.h>
#include <stdint.h>

static uint32_t page_base(const flash_emul_t *self, unsigned page) {
    return self->base_offset + ((uint32_t)page * FLASH_EMUL_PAGE_SIZE);
}

/* Little-endian, as half-words are laid out in the reference's memory image. */
static edge_status_t read_halfword(const flash_emul_t *self, uint32_t offset, uint16_t *out) {
    uint8_t bytes[2];

    edge_status_t status = self->port->read(self->port->self, offset, bytes, sizeof(bytes));
    if (status != EDGE_OK) {
        return status;
    }
    *out = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
    return EDGE_OK;
}

static edge_status_t write_halfword(const flash_emul_t *self, uint32_t offset, uint16_t value) {
    return self->port->write_halfword(self->port->self, offset, value);
}

/*
 * EE_FindValidPage (driver/eeprom.c:401). A read wants the page marked VALID. A write wants
 * the page marked RECEIVE, which is the one being appended to; if the other page is VALID it
 * may be a transfer's source, so the receiving one is the target.
 */
static edge_status_t find_valid_page(const flash_emul_t *self, flash_emul_op_t op,
                                     unsigned *page_out) {
    uint16_t status0 = 0u;
    uint16_t status1 = 0u;

    edge_status_t status = read_halfword(self, page_base(self, 0u), &status0);
    if (status != EDGE_OK) {
        return status;
    }
    status = read_halfword(self, page_base(self, 1u), &status1);
    if (status != EDGE_OK) {
        return status;
    }

    if (op == FLASH_EMUL_OP_WRITE) {
        /*
         * Writes append to the page marked RECEIVE. That is normally the one being filled while
         * the other is valid, but it is also the case on a freshly formatted device, where no
         * page is valid yet - the reference's else branch covers that, and missing it means a
         * freshly formatted device cannot be written to at all.
         */
        if (status0 == FLASH_EMUL_PAGE_MARKER_RECEIVE) {
            *page_out = 0u;
            return EDGE_OK;
        }
        if (status1 == FLASH_EMUL_PAGE_MARKER_RECEIVE) {
            *page_out = 1u;
            return EDGE_OK;
        }
        if (status0 == FLASH_EMUL_PAGE_MARKER_VALID) {
            *page_out = 0u;
            return EDGE_OK;
        }
        if (status1 == FLASH_EMUL_PAGE_MARKER_VALID) {
            *page_out = 1u;
            return EDGE_OK;
        }
    } else {
        if (status0 == FLASH_EMUL_PAGE_MARKER_VALID) {
            *page_out = 0u;
            return EDGE_OK;
        }
        if (status1 == FLASH_EMUL_PAGE_MARKER_VALID) {
            *page_out = 1u;
            return EDGE_OK;
        }
    }

    return EDGE_ENOENT;
}

/*
 * EE_ReadVariable (driver/eeprom.c). The newest value wins, which the append-only layout
 * gives by scanning from the end of the page backwards in four-byte steps and stopping at the
 * first match; the reference walks while the address is still above the page's first slot.
 */
static edge_status_t read_variable(const flash_emul_t *self, uint16_t virtual_address,
                                   uint16_t *out, bool *found) {
    unsigned page = 0u;
    edge_status_t status = find_valid_page(self, FLASH_EMUL_OP_READ, &page);
    if (status != EDGE_OK) {
        return status;
    }

    const uint32_t start = page_base(self, page);
    const uint32_t first_slot = start + 4u;

    *found = false;
    for (uint32_t offset = start + FLASH_EMUL_PAGE_SIZE - 2u; offset > first_slot; offset -= 4u) {
        uint16_t candidate = 0u;
        status = read_halfword(self, offset, &candidate);
        if (status != EDGE_OK) {
            return status;
        }
        if (candidate == virtual_address) {
            uint16_t value = 0u;
            status = read_halfword(self, offset - 2u, &value);
            if (status != EDGE_OK) {
                return status;
            }
            *out = value;
            *found = true;
            return EDGE_OK;
        }
    }

    return EDGE_OK;
}

/*
 * EE_VerifyPageFullWriteVariable (driver/eeprom.c:473): append the data and then its virtual
 * address into the first erased four-byte slot. Nothing already written is touched, so a
 * power loss during a write costs at most that one record.
 */
static edge_status_t write_variable_in_page(const flash_emul_t *self, unsigned page,
                                            uint16_t virtual_address, uint16_t data,
                                            bool *page_full) {
    *page_full = false;

    const uint32_t start = page_base(self, page);
    for (uint32_t offset = start; offset < start + FLASH_EMUL_PAGE_SIZE; offset += 4u) {
        uint16_t low = 0u;
        uint16_t high = 0u;
        edge_status_t status = read_halfword(self, offset, &low);
        if (status != EDGE_OK) {
            return status;
        }
        status = read_halfword(self, offset + 2u, &high);
        if (status != EDGE_OK) {
            return status;
        }
        if (low != 0xFFFFu || high != 0xFFFFu) {
            continue;
        }

        status = write_halfword(self, offset, data);
        if (status != EDGE_OK) {
            return status;
        }
        return write_halfword(self, offset + 2u, virtual_address);
    }

    *page_full = true;
    return EDGE_OK;
}

/*
 * EE_PageTransfer (driver/eeprom.c:534). Mark the other page as receiving, put the new value
 * in it, carry every variable in the consumer's table over (newest value each), erase the old
 * page, and only then mark the new page valid.
 *
 * The order is the reference's, and so is its one rough edge: the erase comes before the VALID
 * marker, so a power loss in between leaves neither page valid. That window is the
 * reference's, and the tests pin the cases around it rather than denying it exists.
 */
static edge_status_t page_transfer(flash_emul_t *self, uint16_t virtual_address, uint16_t data) {
    unsigned old_page = 0u;
    edge_status_t status = find_valid_page(self, FLASH_EMUL_OP_READ, &old_page);
    if (status != EDGE_OK) {
        return status;
    }
    const unsigned new_page = (old_page == 0u) ? 1u : 0u;

    status = write_halfword(self, page_base(self, new_page), FLASH_EMUL_PAGE_MARKER_RECEIVE);
    if (status != EDGE_OK) {
        return status;
    }

    bool page_full = false;
    status = write_variable_in_page(self, new_page, virtual_address, data, &page_full);
    if (status != EDGE_OK) {
        return status;
    }

    for (size_t i = 0u; i < self->table.count; i++) {
        const uint16_t key = self->table.virtual_addresses[i];
        if (key == virtual_address) {
            continue; /* already written above, as the reference does */
        }

        uint16_t carried = 0u;
        bool found = false;
        status = read_variable(self, key, &carried, &found);
        if (status != EDGE_OK) {
            return status;
        }
        if (!found) {
            continue;
        }

        status = write_variable_in_page(self, new_page, key, carried, &page_full);
        if (status != EDGE_OK) {
            return status;
        }
    }

    status =
        self->port->erase_sector(self->port->self, page_base(self, old_page), FLASH_EMUL_PAGE_SIZE);
    if (status != EDGE_OK) {
        return status;
    }

    return write_halfword(self, page_base(self, new_page), FLASH_EMUL_PAGE_MARKER_VALID);
}

/*
 * EE_Format (driver/eeprom.c): both pages erased, then page 0 marked VALID. Marking it valid
 * rather than receiving is what makes a freshly formatted device usable - the page transfer is
 * the only thing that introduces a RECEIVE page, and no page is receiving when there is nothing
 * to transfer yet.
 */
static edge_status_t format(flash_emul_t *self) {
    for (unsigned page = 0u; page < 2u; page++) {
        edge_status_t status =
            self->port->erase_sector(self->port->self, page_base(self, page), FLASH_EMUL_PAGE_SIZE);
        if (status != EDGE_OK) {
            return status;
        }
    }
    return write_halfword(self, page_base(self, 0u), FLASH_EMUL_PAGE_MARKER_VALID);
}

void flash_emul_construct(flash_emul_t *self, const flash_sector_port_t *port,
                          flash_var_table_t table, uint32_t base_offset) {
    if (self == (void *)0) {
        return;
    }
    self->port = port;
    self->table = table;
    self->base_offset = base_offset;
}

edge_status_t flash_emul_init(flash_emul_t *self) {
    if (self == (void *)0 || self->port == (void *)0 || self->port->read == (void *)0 ||
        self->port->write_halfword == (void *)0 || self->port->erase_sector == (void *)0) {
        return EDGE_EINVAL;
    }

    /*
     * Init asks whether a valid page exists, which is a read-side question: a page that is only
     * marked RECEIVE is a half-finished transfer, and there is no valid page to come up on. Asking
     * the write-side question here would treat such an image as usable and read nothing from it.
     */
    unsigned page = 0u;
    edge_status_t status = find_valid_page(self, FLASH_EMUL_OP_READ, &page);
    if (status == EDGE_OK) {
        return EDGE_OK;
    }
    if (status != EDGE_ENOENT) {
        return status;
    }
    return format(self);
}

edge_status_t flash_emul_read(flash_emul_t *self, uint16_t virtual_address, uint16_t *out) {
    if (self == (void *)0 || out == (void *)0) {
        return EDGE_EINVAL;
    }

    bool found = false;
    edge_status_t status = read_variable(self, virtual_address, out, &found);
    if (status != EDGE_OK) {
        return status;
    }
    return found ? EDGE_OK : EDGE_ENOENT;
}

edge_status_t flash_emul_write(flash_emul_t *self, uint16_t virtual_address, uint16_t data) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }

    unsigned page = 0u;
    edge_status_t status = find_valid_page(self, FLASH_EMUL_OP_WRITE, &page);
    if (status != EDGE_OK) {
        return status;
    }

    bool page_full = false;
    status = write_variable_in_page(self, page, virtual_address, data, &page_full);
    if (status != EDGE_OK) {
        return status;
    }
    if (!page_full) {
        return EDGE_OK;
    }

    return page_transfer(self, virtual_address, data);
}

uint32_t flash_emul_valid_page_offset(const flash_emul_t *self) {
    if (self == (void *)0) {
        return 0u;
    }

    unsigned page = 0u;
    if (find_valid_page(self, FLASH_EMUL_OP_READ, &page) != EDGE_OK) {
        return 0u;
    }
    return page_base(self, page);
}
