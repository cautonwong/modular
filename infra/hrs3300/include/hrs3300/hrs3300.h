#ifndef INFRA_HRS3300_H
#define INFRA_HRS3300_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HRS3300_I2C_ADDR 0x44u

#define HRS3300_REG_ID 0x00u
#define HRS3300_REG_ENABLE 0x01u
#define HRS3300_REG_ENABLE_HEN 0x80u
#define HRS3300_REG_C1DATAM 0x08u
#define HRS3300_REG_C0DATAM 0x09u
#define HRS3300_REG_C0DATAH 0x0Au
#define HRS3300_REG_PDRIVER 0x0Cu
#define HRS3300_REG_C1DATAH 0x0Du
#define HRS3300_REG_C1DATAL 0x0Eu
#define HRS3300_REG_C0DATAL 0x0Fu
#define HRS3300_REG_RES 0x16u
#define HRS3300_REG_HGAIN 0x17u

#define HRS3300_BURST_BASE_REG HRS3300_REG_C1DATAM
#define HRS3300_BURST_READ_LEN 8u /* From reg 0x08 to 0x0F */

typedef struct hrs3300_sample {
    uint16_t hrs;
    uint16_t als;
} hrs3300_sample_t;

/**
 * Unpack raw register buffer (8 bytes spanning 0x08..0x0F) into 16-bit HRS and ALS values.
 *
 * @param burst_buf 8-byte buffer read starting from register 0x08.
 * @param out_sample Output unpacked HRS and ALS values.
 * @return EDGE_OK on success, EDGE_EINVAL on null pointer.
 */
edge_status_t hrs3300_unpack_burst(const uint8_t burst_buf[8], hrs3300_sample_t *out_sample);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_HRS3300_H */
