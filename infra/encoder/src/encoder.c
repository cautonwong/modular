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

/* ABI Quadrature is the index machine above, whose own state is cleared by encoder_abi_begin. */
/*
 * enc_abi.c:38-42 - the clearing half of the reference's init, whose other half is the timer's own
 * encoder mode and its EXTI line - and :92-93's reading.
 */
void encoder_abi_begin(encoder_abi_config_t *cfg) {
    if (cfg == (void *)0) {
        return;
    }
    memset(&cfg->state, 0, sizeof(cfg->state));
}

float encoder_abi_read_deg(uint32_t count, uint32_t counts) {
    if (counts == 0u) {
        return 0.0f;
    }
    /* The reference's own arithmetic: the counter over a revolution, in degrees. */
    return ((float)count * 360.0f) / (float)counts;
}

/*
 * enc_abi.c:100-124. The index is only taken if the pin is still high once the caller's own
 * settling has passed - the reference spends four instructions there to reject pulses too short to
 * be anything but noise - and then where the counter was decides whether this is the index it is
 * looking for: within a twentieth of the wrap on either side resets the counter and clears the bad
 * count, while five implausible pulses in a row lose the index again.
 *
 * The reference reads the counter twice, once to test and once to remember; a port can only read
 * the instant it is given, so the one reading serves both.
 */
void encoder_abi_index_pulse(encoder_abi_config_t *cfg, const encoder_abi_port_t *port) {
    if (cfg == (void *)0 || port == (void *)0 || port->read_index_high == (void *)0 ||
        port->read_count == (void *)0) {
        return;
    }

    if (!port->read_index_high(port->self)) {
        return;
    }

    const uint32_t cnt = port->read_count(port->self);
    const uint32_t lim = cfg->counts / 20u;

    cfg->state.cnt_at_ind_last = cnt;
    cfg->state.index_pulse_cnt++;

    if (cfg->state.index_found) {
        /* Some plausibility filtering: an index is only where a revolution begins. */
        if (cnt > (cfg->counts - lim) || cnt < lim) {
            if (port->write_count != (void *)0) {
                port->write_count(port->self, 0u);
            }
            cfg->state.bad_pulses = 0;
        } else {
            cfg->state.bad_pulses++;

            if (cfg->state.bad_pulses > 5) {
                cfg->state.index_found = false;
            }
        }
    } else {
        if (port->write_count != (void *)0) {
            port->write_count(port->self, 0u);
        }
        cfg->state.index_found = true;
        cfg->state.bad_pulses = 0;
    }
}

/*
 * utils_math.c:193-215, utils_fast_atan2. The piecewise approximation the reference takes its
 * angles with, in radians, with its own guard against the not-a-number that both branches can
 * produce.
 */
static float encoder_sincos_fast_atan2(float y, float x) {
    const float abs_y = fabsf(y) + 1e-20f; /* the reference's own kludge against 0/0 */
    float angle;

    if (x >= 0.0f) {
        const float r = (x - abs_y) / (x + abs_y);
        const float rsq = r * r;
        angle = ((0.1963f * rsq) - 0.9817f) * r + (3.14159265358979323846f / 4.0f);
    } else {
        const float r = (x + abs_y) / (abs_y - x);
        const float rsq = r * r;
        angle = ((0.1963f * rsq) - 0.9817f) * r + (3.0f * 3.14159265358979323846f / 4.0f);
    }

    if (isnan(angle)) {
        angle = 0.0f;
    }

    return (y < 0.0f) ? -angle : angle;
}

/*
 * enc_sincos.c:44-101, enc_sincos_read_deg, in the reference's own order: the two readings scaled
 * and offset, both filtered, the phase error taken out of the cosine, the amplitude window tested -
 * with the timestep as the filter constant of the two error rates - and, only inside the window,
 * the lag of the filter itself compensated before the angle is taken. A reading outside the window
 * keeps the angle the last good one reported, which is what discarding a measurement means here.
 */
void encoder_sincos_begin(encoder_sincos_config_t *cfg) {
    if (cfg == (void *)0) {
        return;
    }
    memset(&cfg->state, 0, sizeof(cfg->state));
}

float encoder_sincos_read_deg(encoder_sincos_config_t *cfg, const encoder_sincos_port_t *port,
                              float sin_volts, float cos_volts) {
    if (cfg == (void *)0 || port == (void *)0) {
        return 0.0f;
    }

    float sin = (sin_volts - cfg->sin_offset) * cfg->sin_gain;
    float cos = (cos_volts - cfg->cos_offset) * cfg->cos_gain;

    /* UTILS_LP_FAST is value -= filter_constant * (value - sample) (utils_math.h:100). */
    cfg->state.sin_filter -= cfg->filter_constant * (cfg->state.sin_filter - sin);
    cfg->state.cos_filter -= cfg->filter_constant * (cfg->state.cos_filter - cos);
    sin = cfg->state.sin_filter;
    cos = cfg->state.cos_filter;

    /* The phase error the resolver's own sine and cosine may carry. */
    cos = (cos + sin * cfg->sin_phase) / cfg->cos_phase;

    const float module = sin * sin + cos * cos;

    float timestep = 1.0f;
    if (port->now_seconds != (void *)0) {
        const float now = port->now_seconds(port->self);
        timestep = now - cfg->state.last_update_s;
        if (timestep > 1.0f) {
            timestep = 1.0f;
        }
        cfg->state.last_update_s = now;
    }

    /* SINCOS_MIN_AMPLITUDE and SINCOS_MAX_AMPLITUDE, squared as the reference squares them. */
    if (module > (1.3f * 1.3f)) {
        ++cfg->state.signal_above_max_error_cnt;
        cfg->state.signal_above_max_error_rate -=
            timestep * (cfg->state.signal_above_max_error_rate - 1.0f);
    } else if (module < (0.7f * 0.7f)) {
        ++cfg->state.signal_below_min_error_cnt;
        cfg->state.signal_low_error_rate -= timestep * (cfg->state.signal_low_error_rate - 1.0f);
    } else {
        cfg->state.signal_above_max_error_rate -=
            timestep * (cfg->state.signal_above_max_error_rate - 0.0f);
        cfg->state.signal_low_error_rate -= timestep * (cfg->state.signal_low_error_rate - 0.0f);

        float rpm = 0.0f;
        if (port->read_rpm != (void *)0) {
            rpm = port->read_rpm(port->self);
        }
        const float rpm_rad_s = rpm * (float)((2.0 * 3.14159265358979323846) / 60.0);
        const float delay_comp = ((1.0f - cfg->filter_constant) * rpm_rad_s * timestep) /
                                 (cfg->filter_constant * cfg->ratio);
        /*
         * The reference adds the delay to the angle while it is still in radians and converts the
         * sum, which is why the compensation is not scaled by the same factor as the angle beside
         * it (enc_sincos.c:88-90).
         */
        float angle = (encoder_sincos_fast_atan2(sin, cos) + delay_comp * cfg->delay_comp_sign) *
                          (float)(180.0 / 3.14159265358979323846) +
                      180.0f;

        /* utils_norm_angle's two whiles. */
        while (angle >= 180.0f) {
            angle -= 360.0f;
        }
        while (angle < -180.0f) {
            angle += 360.0f;
        }

        cfg->state.last_enc_angle = angle;
    }

    return cfg->state.last_enc_angle;
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
