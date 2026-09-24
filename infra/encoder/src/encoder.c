#include "encoder/encoder.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* AS5047 */
bool encoder_as5047_check_parity(uint16_t val) {
    uint16_t count = 0;
    for (int i = 0; i < 16; i++) {
        if (val & (1u << i)) {
            count++;
        }
    }
    return (count % 2) == 0;
}

void encoder_as5047_construct(encoder_as5047_t *self, encoder_spi_transfer_fn spi_transfer,
                              void *spi_ctx) {
    if (!self) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->spi_transfer = spi_transfer;
    self->spi_ctx = spi_ctx;
}

edge_status_t encoder_as5047_init(encoder_as5047_t *self) {
    if (!self || !self->spi_transfer) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t encoder_as5047_read_angle_raw(encoder_as5047_t *self, uint16_t *raw_angle) {
    if (!self || !raw_angle || !self->spi_transfer) {
        return EDGE_EINVAL;
    }

    uint16_t tx_cmd = 0x3FFF | 0x4000; /* Read ANGLECOM + Parity */
    if (!encoder_as5047_check_parity(tx_cmd)) {
        tx_cmd |= 0x8000;
    }

    uint16_t rx_data = 0;
    edge_status_t status = self->spi_transfer(self->spi_ctx, tx_cmd, &rx_data);
    if (status != EDGE_OK) {
        return status;
    }

    if (!encoder_as5047_check_parity(rx_data)) {
        self->parity_errors++;
        return EDGE_EIO;
    }

    if (rx_data & 0x4000) {
        /* Error bit set */
        return EDGE_EIO;
    }

    *raw_angle = rx_data & 0x3FFF;
    self->last_angle_raw = *raw_angle;
    return EDGE_OK;
}

edge_status_t encoder_as5047_read_angle_rad(encoder_as5047_t *self, float *angle_rad) {
    if (!self || !angle_rad) {
        return EDGE_EINVAL;
    }
    uint16_t raw = 0;
    edge_status_t st = encoder_as5047_read_angle_raw(self, &raw);
    if (st != EDGE_OK) {
        return st;
    }
    *angle_rad = ((float)raw / (float)AS5047_CPR) * (2.0f * (float)M_PI);
    return EDGE_OK;
}

edge_status_t encoder_as5047_read_diag(encoder_as5047_t *self, uint16_t *diag_val) {
    if (!self || !diag_val || !self->spi_transfer) {
        return EDGE_EINVAL;
    }
    uint16_t tx_cmd = 0x3FFC | 0x4000;
    if (!encoder_as5047_check_parity(tx_cmd)) {
        tx_cmd |= 0x8000;
    }
    uint16_t rx = 0;
    edge_status_t st = self->spi_transfer(self->spi_ctx, tx_cmd, &rx);
    if (st != EDGE_OK) {
        return st;
    }
    *diag_val = rx & 0x3FFF;
    return EDGE_OK;
}

/* MT6816 */
void encoder_mt6816_construct(encoder_mt6816_t *self, encoder_spi_transfer_fn spi_transfer,
                              void *spi_ctx) {
    if (!self) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->spi_transfer = spi_transfer;
    self->spi_ctx = spi_ctx;
}

edge_status_t encoder_mt6816_init(encoder_mt6816_t *self) {
    if (!self || !self->spi_transfer) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t encoder_mt6816_read_angle_raw(encoder_mt6816_t *self, uint16_t *raw_angle) {
    if (!self || !raw_angle || !self->spi_transfer) {
        return EDGE_EINVAL;
    }
    uint16_t rx = 0;
    edge_status_t st = self->spi_transfer(self->spi_ctx, 0x8300, &rx);
    if (st != EDGE_OK) {
        return st;
    }
    *raw_angle = (rx >> 2) & 0x3FFF;
    self->last_angle_raw = *raw_angle;
    self->no_magnet = (rx & 0x02) != 0;
    return EDGE_OK;
}

edge_status_t encoder_mt6816_read_angle_rad(encoder_mt6816_t *self, float *angle_rad) {
    if (!self || !angle_rad) {
        return EDGE_EINVAL;
    }
    uint16_t raw = 0;
    edge_status_t st = encoder_mt6816_read_angle_raw(self, &raw);
    if (st != EDGE_OK) {
        return st;
    }
    *angle_rad = ((float)raw / (float)MT6816_CPR) * (2.0f * (float)M_PI);
    return EDGE_OK;
}

/* ABI Quadrature */
void encoder_abi_construct(encoder_abi_t *self, uint32_t counts_per_rev) {
    if (!self) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->counts_per_rev = (counts_per_rev == 0) ? 4096u : counts_per_rev;
}

edge_status_t encoder_abi_init(encoder_abi_t *self) {
    if (!self || self->counts_per_rev == 0) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t encoder_abi_update(encoder_abi_t *self, int32_t step_delta, float dt,
                                 float *out_angle_rad) {
    if (!self || !out_angle_rad) {
        return EDGE_EINVAL;
    }
    self->count_accumulator += step_delta;
    while (self->count_accumulator < 0) {
        self->count_accumulator += (int32_t)self->counts_per_rev;
    }
    while (self->count_accumulator >= (int32_t)self->counts_per_rev) {
        self->count_accumulator -= (int32_t)self->counts_per_rev;
    }

    self->last_angle_rad =
        ((float)self->count_accumulator / (float)self->counts_per_rev) * (2.0f * (float)M_PI);
    if (dt > 1e-6f) {
        float rps = ((float)step_delta / (float)self->counts_per_rev) / dt;
        self->speed_rpm = rps * 60.0f;
    }
    *out_angle_rad = self->last_angle_rad;
    return EDGE_OK;
}

/* SinCos Resolver */
void encoder_sincos_construct(encoder_sincos_t *self, float sin_offset, float cos_offset,
                              float sin_gain, float cos_gain) {
    if (!self) {
        return;
    }
    memset(self, 0, sizeof(*self));
    self->sin_offset = sin_offset;
    self->cos_offset = cos_offset;
    self->sin_gain = (sin_gain != 0.0f) ? sin_gain : 1.0f;
    self->cos_gain = (cos_gain != 0.0f) ? cos_gain : 1.0f;
}

edge_status_t encoder_sincos_init(encoder_sincos_t *self) {
    if (!self) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t encoder_sincos_update(encoder_sincos_t *self, float sin_val, float cos_val,
                                    float *out_angle_rad) {
    if (!self || !out_angle_rad) {
        return EDGE_EINVAL;
    }
    float s = (sin_val - self->sin_offset) * self->sin_gain;
    float c = (cos_val - self->cos_offset) * self->cos_gain;
    float ang = atan2f(s, c);
    if (ang < 0.0f) {
        ang += 2.0f * (float)M_PI;
    }
    self->last_angle_rad = ang;
    *out_angle_rad = ang;
    return EDGE_OK;
}

/* Hall */
static const uint8_t DEFAULT_HALL_TAB[8] = {0, 1, 3, 2, 5, 6, 4, 0};

void encoder_hall_construct(encoder_hall_t *self, const uint8_t *custom_tab) {
    if (!self) {
        return;
    }
    memset(self, 0, sizeof(*self));
    if (custom_tab) {
        memcpy(self->hall_tab, custom_tab, 8);
    } else {
        memcpy(self->hall_tab, DEFAULT_HALL_TAB, 8);
    }
}

edge_status_t encoder_hall_init(encoder_hall_t *self) {
    if (!self) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t encoder_hall_update(encoder_hall_t *self, uint8_t hall_state, float *out_angle_rad) {
    if (!self || !out_angle_rad || hall_state > 7) {
        return EDGE_EINVAL;
    }

    uint8_t step = self->hall_tab[hall_state];
    if (step == 0) {
        return EDGE_EINVAL; /* Illegal state */
    }

    self->last_hall_state = hall_state;
    /* 60 degrees per step */
    self->last_angle_rad = ((float)(step - 1) / 6.0f) * (2.0f * (float)M_PI);
    *out_angle_rad = self->last_angle_rad;
    return EDGE_OK;
}
