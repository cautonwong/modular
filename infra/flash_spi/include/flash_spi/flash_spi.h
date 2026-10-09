#ifndef INFRA_FLASH_SPI_H
#define INFRA_FLASH_SPI_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_SPI_PAGE_SIZE 256u
#define FLASH_SPI_SECTOR_SIZE 4096u

#define FLASH_SPI_CMD_PAGE_PROGRAM 0x02u
#define FLASH_SPI_CMD_READ 0x03u
#define FLASH_SPI_CMD_READ_STATUS 0x05u
#define FLASH_SPI_CMD_WRITE_ENABLE 0x06u
#define FLASH_SPI_CMD_SECTOR_ERASE 0x20u
#define FLASH_SPI_CMD_READ_ID 0x9Fu
#define FLASH_SPI_CMD_RELEASE_DEEP_SLEEP 0xABu
#define FLASH_SPI_CMD_DEEP_POWER_DOWN 0xB9u

#define FLASH_SPI_STATUS_WIP 0x01u /* Write In Progress */
#define FLASH_SPI_STATUS_WEL 0x02u /* Write Enable Latch */

typedef struct flash_spi_jedec_id {
    uint8_t manufacturer_id;
    uint8_t memory_type;
    uint8_t capacity_id;
} flash_spi_jedec_id_t;

/**
 * Format a standard 4-byte SPI command header (Command + 24-bit big-endian address).
 *
 * @param cmd SPI Opcode (e.g. READ, PAGE_PROGRAM, SECTOR_ERASE).
 * @param address 24-bit Flash memory byte address.
 * @param out_header 4-byte buffer to receive [cmd, addr[23..16], addr[15..8], addr[7..0]].
 * @return EDGE_OK on success, EDGE_EINVAL on NULL.
 */
edge_status_t flash_spi_format_cmd_addr(uint8_t cmd, uint32_t address, uint8_t out_header[4]);

/**
 * Unpack 3-byte JEDEC ID response.
 *
 * @param raw_id 3 bytes read from JEDEC ID command (0x9F).
 * @param out_id Output JEDEC structure.
 * @return EDGE_OK on success, EDGE_EINVAL on NULL.
 */
edge_status_t flash_spi_unpack_jedec_id(const uint8_t raw_id[3], flash_spi_jedec_id_t *out_id);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_FLASH_SPI_H */
