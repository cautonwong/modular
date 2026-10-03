#include "flash_spi/flash_spi.h"

edge_status_t flash_spi_format_cmd_addr(uint8_t cmd, uint32_t address, uint8_t out_header[4]) {
    if (out_header == NULL) {
        return EDGE_EINVAL;
    }

    out_header[0] = cmd;
    out_header[1] = (uint8_t)((address >> 16u) & 0xFFu);
    out_header[2] = (uint8_t)((address >> 8u) & 0xFFu);
    out_header[3] = (uint8_t)(address & 0xFFu);

    return EDGE_OK;
}

edge_status_t flash_spi_unpack_jedec_id(const uint8_t raw_id[3], flash_spi_jedec_id_t *out_id) {
    if (raw_id == NULL || out_id == NULL) {
        return EDGE_EINVAL;
    }

    out_id->manufacturer_id = raw_id[0];
    out_id->memory_type = raw_id[1];
    out_id->capacity_id = raw_id[2];

    return EDGE_OK;
}
