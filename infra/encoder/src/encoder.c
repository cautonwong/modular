#include "encoder/encoder.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

bool encoder_as5047_check_parity(uint16_t val) {
    /* Even parity over all 16 bits: total number of 1s in val must be even (XOR sum == 0) */
    uint16_t v = val;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    return (v & 1u) == 0;
}

static uint16_t as5047_make_cmd(uint16_t addr, bool read) {
    uint16_t cmd = (addr & 0x3FFFu) | (read ? (1u << 14) : 0u);
    /* Calculate even parity bit 15 */
    uint16_t v = cmd;
    v ^= v >> 8;
    v ^= v >> 4;
    v ^= v >> 2;
    v ^= v >> 1;
    if ((v & 1u) != 0) {
        cmd |= (1u << 15);
    }
    return cmd;
}

void encoder_as5047_construct(encoder_as5047_t *self, encoder_spi_transfer_fn spi_transfer,
                              void *spi_ctx) {
    if (self == (void *)0) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->spi_transfer = spi_transfer;
    self->spi_ctx = spi_ctx;
}

edge_status_t encoder_as5047_init(encoder_as5047_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    self->last_angle_raw = 0;
    self->parity_errors = 0;
    return EDGE_OK;
}

edge_status_t encoder_as5047_read_angle_raw(encoder_as5047_t *self, uint16_t *raw_angle) {
    if (self == (void *)0 || self->spi_transfer == (void *)0 || raw_angle == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t cmd = as5047_make_cmd(AS5047_REG_ANGLECOM, true);
    uint16_t rx = 0;

    edge_status_t status = self->spi_transfer(self->spi_ctx, cmd, &rx);
    if (status != EDGE_OK) {
        return status;
    }

    /* AS5047 returns previous command's data on subsequent transfer */
    status = self->spi_transfer(self->spi_ctx, cmd, &rx);
    if (status != EDGE_OK) {
        return status;
    }

    if (!encoder_as5047_check_parity(rx)) {
        self->parity_errors++;
        return EDGE_EIO;
    }

    /* Check error flag (bit 14) */
    if ((rx & (1u << 14)) != 0) {
        return EDGE_EIO;
    }

    *raw_angle = rx & 0x3FFFu;
    self->last_angle_raw = *raw_angle;
    return EDGE_OK;
}

edge_status_t encoder_as5047_read_angle_rad(encoder_as5047_t *self, float *angle_rad) {
    if (angle_rad == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t raw = 0;
    edge_status_t status = encoder_as5047_read_angle_raw(self, &raw);
    if (status != EDGE_OK) {
        return status;
    }

    *angle_rad = ((float)raw / (float)AS5047_CPR) * (2.0f * (float)M_PI);
    return EDGE_OK;
}

edge_status_t encoder_as5047_read_diag(encoder_as5047_t *self, uint16_t *diag_val) {
    if (self == (void *)0 || self->spi_transfer == (void *)0 || diag_val == (void *)0) {
        return EDGE_EINVAL;
    }

    uint16_t cmd = as5047_make_cmd(AS5047_REG_DIAAGC, true);
    uint16_t rx = 0;

    edge_status_t status = self->spi_transfer(self->spi_ctx, cmd, &rx);
    if (status != EDGE_OK) {
        return status;
    }

    status = self->spi_transfer(self->spi_ctx, cmd, &rx);
    if (status != EDGE_OK) {
        return status;
    }

    *diag_val = rx & 0x3FFFu;
    return EDGE_OK;
}

/* Hall Sensor Decoder Implementation */
static const uint8_t default_hall_table[8] = {
    0xFF, /* 000 (invalid) */
    0,    /* 001 */
    2,    /* 010 */
    1,    /* 011 */
    4,    /* 100 */
    5,    /* 101 */
    3,    /* 110 */
    0xFF  /* 111 (invalid) */
};

void encoder_hall_construct(encoder_hall_t *self, const uint8_t *custom_tab) {
    if (self == (void *)0) {
        return;
    }
    memset(self, 0, sizeof(*self));
    if (custom_tab != (void *)0) {
        memcpy(self->hall_tab, custom_tab, 8);
    } else {
        memcpy(self->hall_tab, default_hall_table, 8);
    }
}

edge_status_t encoder_hall_init(encoder_hall_t *self) {
    if (self == (void *)0) {
        return EDGE_EINVAL;
    }
    self->last_hall_state = 0;
    self->last_angle_rad = 0.0f;
    return EDGE_OK;
}

edge_status_t encoder_hall_update(encoder_hall_t *self, uint8_t hall_state, float *out_angle_rad) {
    if (self == (void *)0 || out_angle_rad == (void *)0) {
        return EDGE_EINVAL;
    }

    uint8_t state = hall_state & 0x07u;
    uint8_t step = self->hall_tab[state];
    if (step == 0xFFu) {
        return EDGE_EINVAL;
    }

    /* 6 electrical sectors across 2*pi (each step is 60 deg = pi/3 rad) */
    float angle = (float)step * ((float)M_PI / 3.0f);
    self->last_hall_state = state;
    self->last_angle_rad = angle;
    *out_angle_rad = angle;

    return EDGE_OK;
}
