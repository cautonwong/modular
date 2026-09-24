#ifndef INFRA_ENCODER_H
#define INFRA_ENCODER_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* AS5047 Registers */
#define AS5047_REG_NOP 0x0000
#define AS5047_REG_ERRFL 0x0001
#define AS5047_REG_PROG 0x0003
#define AS5047_REG_DIAAGC 0x3FFC
#define AS5047_REG_MAG 0x3FFD
#define AS5047_REG_ANGLEUNC 0x3FFE
#define AS5047_REG_ANGLECOM 0x3FFF

#define AS5047_CPR 16384u /* 14-bit resolution */

typedef edge_status_t (*encoder_spi_transfer_fn)(void *ctx, uint16_t tx_val, uint16_t *rx_val);

typedef struct encoder_as5047 {
    encoder_spi_transfer_fn spi_transfer;
    void *spi_ctx;
    uint16_t last_angle_raw;
    uint32_t parity_errors;
} encoder_as5047_t;

typedef struct encoder_hall {
    uint8_t hall_tab[8];
    uint8_t last_hall_state;
    float last_angle_rad;
} encoder_hall_t;

/* AS5047 API */
void encoder_as5047_construct(encoder_as5047_t *self, encoder_spi_transfer_fn spi_transfer,
                              void *spi_ctx);
edge_status_t encoder_as5047_init(encoder_as5047_t *self);
edge_status_t encoder_as5047_read_angle_raw(encoder_as5047_t *self, uint16_t *raw_angle);
edge_status_t encoder_as5047_read_angle_rad(encoder_as5047_t *self, float *angle_rad);
edge_status_t encoder_as5047_read_diag(encoder_as5047_t *self, uint16_t *diag_val);
bool encoder_as5047_check_parity(uint16_t val);

/* Hall API */
void encoder_hall_construct(encoder_hall_t *self, const uint8_t *custom_tab);
edge_status_t encoder_hall_init(encoder_hall_t *self);
edge_status_t encoder_hall_update(encoder_hall_t *self, uint8_t hall_state, float *out_angle_rad);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_ENCODER_H */
