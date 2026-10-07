#include "encoder/encoder.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/*
 * enc_as504x.c:66's own exchange, whose error bit is the fourteenth: one transfer, then every frame
 * of its answer is asked whether that bit is raised.
 */
static uint8_t as504x_transfer_err_check(const encoder_as504x_port_t *port, uint16_t *in_buf,
                                         const uint16_t *out_buf, int length) {
    port->transfer16(port->self, in_buf, out_buf, length);
    for (int i = 0; i < length; i++) {
        if (((in_buf[i]) >> 14) & 0x01u) {
            return 1u;
        }
    }
    return 0u;
}

/*
 * :193-203's fetch: the diagnostic and magnitude frames are sent, the answer to the first arrives
 * with the second's exchange and the answer to the second with a bare one after it, and both are
 * kept only when neither raised the error bit and both pass their parity.
 */
static uint8_t as504x_fetch_diag(encoder_as504x_state_t *st, const encoder_as504x_port_t *port) {
    uint16_t recf[2] = {0u, 0u};
    const uint16_t senf[2] = {(uint16_t)(AS504X_SPI_DIAG_ADR | AS504X_SPI_READ_BIT),
                              (uint16_t)(AS504X_SPI_MAGN_ADR | AS504X_SPI_READ_BIT)};
    uint8_t ret = 0u;

    port->transfer16(port->self, 0, &senf[0], 1);

    ret |= as504x_transfer_err_check(port, &recf[0], &senf[1], 1);
    ret |= as504x_transfer_err_check(port, &recf[1], 0, 1);

    if (ret == 0u) {
        if (encoder_as504x_parity_ok(recf[0]) && encoder_as504x_parity_ok(recf[1])) {
            st->sensor_diag.serial_diag_flgs = recf[0];
            st->sensor_diag.serial_magnitude = recf[1];
        }
    }
    return ret;
}

/* :204-215's deserialising, field by field. */
static void as504x_deserialize_diag(encoder_as504x_state_t *st) {
    st->sensor_diag.AGC_value = st->sensor_diag.serial_diag_flgs;
    st->sensor_diag.is_OCF = (st->sensor_diag.serial_diag_flgs >> AS504X_SPI_DIAG_OCF_BIT_POS) & 1u;
    st->sensor_diag.is_COF = (st->sensor_diag.serial_diag_flgs >> AS504X_SPI_DIAG_COF_BIT_POS) & 1u;
    st->sensor_diag.is_Comp_low =
        (st->sensor_diag.serial_diag_flgs >> AS504X_SPI_DIAG_COMP_LOW_BIT_POS) & 1u;
    st->sensor_diag.is_Comp_high =
        (st->sensor_diag.serial_diag_flgs >> AS504X_SPI_DIAG_COMP_HIGH_BIT_POS) & 1u;
    st->sensor_diag.magnitude =
        st->sensor_diag.serial_magnitude & AS504X_SPI_EXCLUDE_PARITY_AND_ERROR_BITMASK;
}

/*
 * :172-192's check of what the diagnostics said: both compensation flags at once is a serial that
 * cannot be right, and a magnitude and an AGC that are both nought are a sensor that is not
 * answering at all.
 */
static uint8_t as504x_verify_serial(const encoder_as504x_state_t *st) {
    const uint16_t test_magnitude =
        st->sensor_diag.serial_magnitude & AS504X_SPI_EXCLUDE_PARITY_AND_ERROR_BITMASK;
    const uint8_t test_agc_value = (uint8_t)st->sensor_diag.serial_diag_flgs;
    const uint8_t test_is_comp_low =
        (uint8_t)((st->sensor_diag.serial_diag_flgs >> AS504X_SPI_DIAG_COMP_LOW_BIT_POS) & 1u);
    const uint8_t test_is_comp_high =
        (uint8_t)((st->sensor_diag.serial_diag_flgs >> AS504X_SPI_DIAG_COMP_HIGH_BIT_POS) & 1u);

    if (test_is_comp_high && test_is_comp_low) {
        return 1u;
    }
    if ((uint32_t)test_magnitude + (uint32_t)test_agc_value == 0u) {
        return 1u;
    }
    return 0u;
}

/* :216-227's clear-errors read, whose answer is remembered whether or not it is whole. */
static void as504x_fetch_clear_err_diag(encoder_as504x_state_t *st,
                                        const encoder_as504x_port_t *port) {
    uint16_t recf = 0u;
    const uint16_t senf = (uint16_t)(AS504X_SPI_CLEAR_ERROR_ADR | AS504X_SPI_READ_BIT);

    port->transfer16(port->self, 0, &senf, 1);
    port->transfer16(port->self, &recf, 0, 1);

    st->sensor_diag.serial_error_flags = recf;
}

/*
 * :255-277's connection determiner: the counter rises on a word that was not valid and falls on one
 * that was, the flag only goes down at the threshold and only comes back up once the counter has
 * come all the way back - which is the reference's own hysteresis, and why a good word after a bad
 * one does not by itself report a connection.
 */
static void as504x_determinate_if_connected(encoder_as504x_state_t *st, bool was_last_valid) {
    if (!was_last_valid) {
        st->spi_communication_error_count++;

        if (st->spi_communication_error_count >= AS504X_CONNECTION_DETERMINATOR_ERROR_THRESHOLD) {
            st->spi_communication_error_count = AS504X_CONNECTION_DETERMINATOR_ERROR_THRESHOLD;
            st->sensor_diag.is_connected = 0u;
        }
    } else if (st->spi_communication_error_count != 0u) {
        st->spi_communication_error_count--;
    } else {
        st->sensor_diag.is_connected = 1u;
    }
}

/* :68-72, the initialising, which is the clearing of the state. */
void encoder_as504x_begin(encoder_as504x_state_t *st) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

/* driver/spi_bb.c:309-316, the driver's own parity test. */
bool encoder_as504x_parity_ok(uint16_t x) {
    x ^= (uint16_t)(x >> 8);
    x ^= (uint16_t)(x >> 4);
    x ^= (uint16_t)(x >> 2);
    x ^= (uint16_t)(x >> 1);
    return (bool)((~x) & 1u);
}

/*
 * enc_as504x.c:80-148, its routine, whose two paths are whether a MOSI line is there to ask for
 * diagnostics over. With one, the word is read with the error check, and every hundredth time - or
 * as soon as one raises an error - the diagnostics are cleared, fetched, checked and either
 * believed or not. Without one, a bare read is all there is and a word of nothing but noughts or
 * ones is counted until the connection is given up on.
 *
 * The angle is made at the end of either path (:140-148), out of a word that is whole and whose own
 * error flag is not raised.
 */
float encoder_as504x_routine(encoder_as504x_state_t *st, const encoder_as504x_port_t *port,
                             float now_s) {
    if (st == (void *)0 || port == (void *)0 || port->transfer16 == (void *)0) {
        return 0.0f;
    }

    float timestep = now_s - st->last_update_s;
    if (timestep > 1.0f) {
        timestep = 1.0f;
    }
    st->last_update_s = now_s;

    uint16_t pos = 0u;

    if (port->has_mosi) {
        port->transfer16(port->self, 0, 0, 1);

        st->spi_data_err_raised = as504x_transfer_err_check(port, &pos, 0, 1);
        st->spi_val = pos;

        st->diag_fetch_now_count++;
        if (st->diag_fetch_now_count >= AS504X_REFRESH_DIAG_AFTER_NSAMPLES ||
            st->spi_data_err_raised) {
            as504x_fetch_clear_err_diag(st, port);

            if (!as504x_fetch_diag(st, port)) {
                if (!as504x_verify_serial(st)) {
                    as504x_deserialize_diag(st);
                    as504x_determinate_if_connected(st, true);
                } else {
                    as504x_determinate_if_connected(st, false);
                }
            } else {
                as504x_determinate_if_connected(st, false);
            }
            st->diag_fetch_now_count = 0u;
        }
    } else {
        port->transfer16(port->self, &pos, 0, 1);
        st->spi_val = pos;

        if (0x0000u == pos || 0xFFFFu == pos) {
            st->data_last_invalid_counter++;
        } else {
            st->data_last_invalid_counter = 0u;
            as504x_determinate_if_connected(st, true);
        }

        if (st->data_last_invalid_counter >= AS504X_DATA_INVALID_THRESHOLD) {
            as504x_determinate_if_connected(st, false);
            st->data_last_invalid_counter = AS504X_DATA_INVALID_THRESHOLD;
        }
    }

    if (encoder_as504x_parity_ok(pos) && !st->spi_data_err_raised) {
        pos &= 0x3FFFu;
        st->last_enc_angle = ((float)pos * 360.0f) / 16384.0f;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 0.0f);
    } else {
        ++st->spi_error_cnt;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 1.0f);
    }

    return st->last_enc_angle;
}

/* :150-153, the reading, which is the routine and then what it left. */
float encoder_as504x_read_angle(encoder_as504x_state_t *st, const encoder_as504x_port_t *port,
                                float now_s) {
    return encoder_as504x_routine(st, port, now_s);
}

/* MT6816 is the routine below, whose own state is cleared by encoder_mt6816_begin. */

/*
 * enc_mt6835.c:60-75, its own CRC-8: the polynomial 0x07, most significant bit first, seeded with
 * nought. The sensor carries it over the three bytes the angle is in, and the routine below
 * compares it against the byte that follows them. It is the ordinary CRC-8, whose check value for
 * the digits one to nine is 0xF4 - which is what the test holds it to.
 */
uint8_t encoder_mt6835_crc8(const uint8_t *data, int len) {
    uint8_t crc = 0x00u;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if ((crc & 0x80u) != 0u) {
                crc = (uint8_t)((crc << 1) ^ 0x07u);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

void encoder_mt6835_begin(encoder_mt6835_state_t *st) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

void encoder_bissc_begin(encoder_bissc_config_t *cfg, uint32_t enc_res) {
    if (cfg == (void *)0) {
        return;
    }
    memset(&cfg->state, 0, sizeof(cfg->state));
    cfg->enc_res = enc_res;

    /*
     * :64-80, the table the frame's own checksum is taken with: every six-bit group folded through
     * the polynomial 0x43, six times over. The reference builds it in its init, which is here.
     */
    for (int i = 0; i < 64; i++) {
        int crc = i;
        for (int j = 0; j < 6; j++) {
            if ((crc & 0x20) != 0) {
                crc = (crc << 1) ^ 0x43;
            } else {
                crc = crc << 1;
            }
        }
        cfg->table_crc6n[i] = (uint8_t)crc;
    }
}

/*
 * :155-166: the checksum over the frame's data, six bits at a time from the top, seeded with the
 * two bits above the first group and inverted at the end - the reference's own five folds and its
 * complement.
 */
uint8_t encoder_bissc_crc6(const uint8_t table[64], uint32_t data_rx) {
    if (table == (void *)0) {
        return 0u;
    }

    uint8_t crc = (uint8_t)((data_rx >> 30) & 0x03u);
    crc = table[((data_rx >> 24) & 0x3Fu) ^ crc];
    crc = table[((data_rx >> 18) & 0x3Fu) ^ crc];
    crc = table[((data_rx >> 12) & 0x3Fu) ^ crc];
    crc = table[((data_rx >> 6) & 0x3Fu) ^ crc];
    crc = table[((data_rx >> 0) & 0x3Fu) ^ crc];
    return (uint8_t)(0x3Fu & ~crc);
}

/*
 * enc_bissc.c:124-180, the decode the reference does in its SPI callback: the eight bytes are one
 * word, left-aligned at the first bit that is set, the two bits that begin a frame are dropped, and
 * what is left is trimmed so that the position, the two flags and the six checksum bits end at the
 * bottom of it. A checksum that does not match raises the data error count and the rate; one that
 * does moves the rate back and makes the angle from the position over the counter's own full scale
 * - two to the resolution less one, which is the reference's denominator rather than the
 * resolution.
 *
 * A frame of nothing at all is the one case the reference leaves undefined, since its own shift is
 * then asked for sixty-four; it is counted as a bad word here.
 */
/* driver/spi_bb.c:309-316's fold, whose low bit is set when the frame's bits come to one - and this
 * converter's frame carries odd parity, so that low bit being set is what an error looks like. */
static bool ad2s1205_parity_error(uint16_t x) {
    x ^= (uint16_t)(x >> 8);
    x ^= (uint16_t)(x >> 4);
    x ^= (uint16_t)(x >> 2);
    x ^= (uint16_t)(x >> 1);
    return (bool)((~x) & 1u);
}

/* :36-53's initialising, which is the clearing of everything the state holds. */
void encoder_ad2s1205_begin(encoder_ad2s1205_state_t *st) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

/* :178-196's reset: the counts, the rates and the peaks, but not the word or the angle it made. */
void encoder_ad2s1205_reset_errors(encoder_ad2s1205_state_t *st) {
    if (st == (void *)0) {
        return;
    }

    st->spi_error_cnt = 0u;
    st->spi_error_rate = 0.0f;
    st->resolver_loss_of_tracking_error_rate = 0.0f;
    st->resolver_degradation_of_signal_error_rate = 0.0f;
    st->resolver_loss_of_signal_error_rate = 0.0f;
    st->resolver_loss_of_tracking_error_cnt = 0u;
    st->resolver_degradation_of_signal_error_cnt = 0u;
    st->resolver_loss_of_signal_error_cnt = 0u;
    st->resolver_void_packet_cnt = 0u;
    st->resolver_void_packet_error_rate = 0.0f;
    st->resolver_vel_packet_cnt = 0u;
    st->resolver_vel_packet_error_rate = 0.0f;
    st->resolver_LOT_peak_error_rate = 0.0f;
    st->resolver_LOS_peak_error_rate = 0.0f;
    st->resolver_DOS_peak_error_rate = 0.0f;
    st->resolver_SPI_peak_error_rate = 0.0f;
    st->resolver_VELread_peak_error_rate = 0.0f;
    st->resolver_VOIDspi_peak_error_rate = 0.0f;
}

/* :52-58's setter: the flag, and the count of how many times one was raised. */
void encoder_ma782_error(encoder_ma782_state_t *st, uint32_t flag) {
    if (st == (void *)0) {
        return;
    }
    st->error |= flag;
    st->error_count++;
}

/* :55-58's mask, from four above the sensor's own nine bits up to the frame's top. */
uint16_t encoder_ma782_resolution_mask(void) {
    const uint32_t low = 4u + (MA782_MAX_RESOLUTION_BITS - MA782_RESOLUTION_BITS);
    const uint32_t high = 15u;
    return (uint16_t)(((1u << (high - low + 1u)) - 1u) << low);
}

void encoder_ma782_begin(encoder_ma782_state_t *st) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

/*
 * enc_ma782.c:123-141, its own beginning of a read: the state has to be idle for it, the frame the
 * sensor is given is noughts, the receive flag is cleared first - the reference's own corner case,
 * where a reception its own DMA missed leaves that flag set so that the next exchange would begin
 * with the byte left over - and then the exchange is started, whose answer the callback decodes.
 */
bool encoder_ma782_read_angle(encoder_ma782_state_t *st, const encoder_ma782_port_t *port) {
    if (st == (void *)0 || port == (void *)0 || port->start_exchange == (void *)0) {
        return false;
    }
    if (st->substate != ENCODER_MA782_IDLE) {
        encoder_ma782_error(st, ENCODER_MA782_ANGLE_NOT_IDLE);
        return false;
    }

    st->tx_data = 0u;
    for (int i = 0; i < 4; i++) {
        st->tx_buf[i] = 0u;
    }
    if (port->flush_rx != (void *)0) {
        port->flush_rx(port->self);
    }

    st->substate = ENCODER_MA782_READ_ANGLE_REQ;
    port->start_exchange(port->self, st->tx_buf, st->rx_buf);
    return true;
}

/*
 * :173-187, its routine: with the bus ready and the run started, an exchange is begun and the rate
 * of missed ones falls towards nought - with the reference's own fixed ten-kilohertz factor.
 * Without it the count rises, the flag says why, and the rate climbs the same way.
 */
bool encoder_ma782_routine(encoder_ma782_state_t *st, const encoder_ma782_port_t *port) {
    if (st == (void *)0 || port == (void *)0 || port->spi_ready == (void *)0) {
        return false;
    }

    if (port->spi_ready(port->self)) {
        if (st->start > 0u) {
            (void)encoder_ma782_read_angle(st, port);
        }
        st->spi_comm_error_rate -= 0.0001f * (st->spi_comm_error_rate - 0.0f);
        return true;
    }

    ++st->spi_comm_error_cnt;
    encoder_ma782_error(st, ENCODER_MA782_SPI_NOT_READY);
    st->spi_comm_error_rate -= 0.0001f * (st->spi_comm_error_rate - 1.0f);
    return false;
}

/*
 * :146-154, the tail the callback runs: the angle is the frame's own masked bits over the whole of
 * a sixteen-bit turn - the reference's denominator rather than the mask's - and the sub-state goes
 * back to idle for the next one.
 */
float encoder_ma782_read_angle_finish(encoder_ma782_state_t *st) {
    if (st == (void *)0) {
        return 0.0f;
    }

    uint16_t angle = (uint16_t)st->rx_data;
    angle = (uint16_t)(angle & encoder_ma782_resolution_mask());
    st->last_enc_angle = (float)angle * (360.0f / 65535.0f);
    st->substate = ENCODER_MA782_IDLE;
    return st->last_enc_angle;
}

void encoder_ts5700_begin(encoder_ts5700_state_t *st) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

/*
 * enc_ts5700n8501.c:168-206, the decode of one reply: eleven bytes with the last being the
 * exclusive or of the ten before it. When that holds, the position is the three after the first -
 * little-endian
 * - over the sensor's own hundred and thirty-one thousand and seventy-two counts of a turn, the
 * error rate falls, and the eight status bytes are kept as they came: the sensor's own field, the
 * three absolute and three multiturn ones and the alarm. When it does not, the count rises and the
 * rate climbs instead, and the angle the last good reply made is what comes back.
 */
float encoder_ts5700_decode(encoder_ts5700_state_t *st,
                            const uint8_t reply[ENCODER_TS5700_REPLY_LEN], float timestep) {
    if (st == (void *)0 || reply == (void *)0) {
        return 0.0f;
    }

    uint8_t crc = 0u;
    for (int i = 0; i < (int)ENCODER_TS5700_REPLY_LEN - 1; i++) {
        crc = (uint8_t)(reply[i] ^ crc);
    }

    if (crc == reply[ENCODER_TS5700_REPLY_LEN - 1]) {
        const uint32_t pos =
            (uint32_t)reply[2] + ((uint32_t)reply[3] << 8) + ((uint32_t)reply[4] << 16);
        st->spi_val = pos;
        st->last_enc_angle = (float)pos / ENCODER_TS5700_COUNTS_PER_TURN * 360.0f;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 0.0f);

        st->raw_status[0] = reply[1]; /* SF */
        st->raw_status[1] = reply[2]; /* ABS0 */
        st->raw_status[2] = reply[3]; /* ABS1 */
        st->raw_status[3] = reply[4]; /* ABS2 */
        st->raw_status[4] = reply[6]; /* ABM0 */
        st->raw_status[5] = reply[7]; /* ABM1 */
        st->raw_status[6] = reply[8]; /* ABM2 */
        st->raw_status[7] = reply[9]; /* ALMC */
    } else {
        ++st->spi_error_cnt;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 1.0f);
    }

    return st->last_enc_angle;
}

/*
 * enc_ad2s1205.c:66-176, its routine, in the three stages the reference takes them in. A frame of
 * nothing at all is a converter that is not answering, and its error rate climbs while its peak is
 * kept; a frame that answers but carries no read-velocity bit is a velocity one, which is counted
 * the same way. Both of those also settle the other rate back towards nought.
 *
 * Only a position packet is then read for what it says: the second and first bits are the loss of
 * tracking and of signal, both of them inverted - a clear bit is the fault - and a loss of signal
 * is what the two of them together mean, in which case the tracking flag is dropped rather than
 * counted with it. The bus's own parity is the frame's odd one, and it is the frame that decides
 * whether the angle is taken, together with all three of the chip's flags.
 */
float encoder_ad2s1205_routine(encoder_ad2s1205_state_t *st, const encoder_ad2s1205_port_t *port,
                               float now_s) {
    if (st == (void *)0 || port == (void *)0 || port->read_frame == (void *)0) {
        return 0.0f;
    }

    float timestep = now_s - st->last_update_s;
    if (timestep > 1.0f) {
        timestep = 1.0f;
    }
    st->last_update_s = now_s;

    uint16_t pos = 0u;
    if (port->read_frame(port->self, &pos) != EDGE_OK) {
        /* The reference's bus always answers, so it has no such guard; a port that cannot read is a
         * frame it cannot judge, and the angle it had is what comes back. */
        return st->last_enc_angle;
    }

    st->spi_val = pos;

    const uint16_t rdvel = (uint16_t)(pos & 0x0008u);

    if (st->spi_val == 0u) {
        ++st->resolver_void_packet_cnt;
        st->resolver_void_packet_error_rate -=
            timestep * (st->resolver_void_packet_error_rate - 1.0f);
        if (st->resolver_void_packet_error_rate > st->resolver_VOIDspi_peak_error_rate) {
            st->resolver_VOIDspi_peak_error_rate = st->resolver_void_packet_error_rate;
        }
    } else {
        st->resolver_void_packet_error_rate -=
            timestep * (st->resolver_void_packet_error_rate - 0.0f);
        if (rdvel == 0u) {
            ++st->resolver_vel_packet_cnt;
            st->resolver_vel_packet_error_rate -=
                timestep * (st->resolver_vel_packet_error_rate - 1.0f);
            if (st->resolver_vel_packet_error_rate > st->resolver_VELread_peak_error_rate) {
                st->resolver_VELread_peak_error_rate = st->resolver_vel_packet_error_rate;
            }
        } else {
            st->resolver_vel_packet_error_rate -=
                timestep * (st->resolver_vel_packet_error_rate - 0.0f);
        }
    }

    if (rdvel != 0u) {
        bool dos = ((pos & 0x04u) == 0u);
        bool lot = ((pos & 0x02u) == 0u);
        const bool los = dos && lot;
        const bool parity_error = ad2s1205_parity_error(pos);
        bool angle_is_correct = true;

        if (los) {
            lot = false;
            dos = false;
        }

        if (!parity_error) {
            st->spi_error_rate -= timestep * (st->spi_error_rate - 0.0f);
        } else {
            angle_is_correct = false;
            ++st->spi_error_cnt;
            st->spi_error_rate -= timestep * (st->spi_error_rate - 1.0f);
            if (st->spi_error_rate > st->resolver_SPI_peak_error_rate) {
                st->resolver_SPI_peak_error_rate = st->spi_error_rate;
            }
        }

        uint16_t counts = (uint16_t)(pos & 0xFFF0u);
        counts = (uint16_t)(counts >> 4);
        counts = (uint16_t)(counts & 0x0FFFu);

        if (lot) {
            angle_is_correct = false;
            ++st->resolver_loss_of_tracking_error_cnt;
            st->resolver_loss_of_tracking_error_rate -=
                timestep * (st->resolver_loss_of_tracking_error_rate - 1.0f);
            if (st->resolver_loss_of_tracking_error_rate > st->resolver_LOT_peak_error_rate) {
                st->resolver_LOT_peak_error_rate = st->resolver_loss_of_tracking_error_rate;
            }
        } else {
            st->resolver_loss_of_tracking_error_rate -=
                timestep * (st->resolver_loss_of_tracking_error_rate - 0.0f);
        }

        if (dos) {
            angle_is_correct = false;
            ++st->resolver_degradation_of_signal_error_cnt;
            st->resolver_degradation_of_signal_error_rate -=
                timestep * (st->resolver_degradation_of_signal_error_rate - 1.0f);
            if (st->resolver_degradation_of_signal_error_rate > st->resolver_DOS_peak_error_rate) {
                st->resolver_DOS_peak_error_rate = st->resolver_degradation_of_signal_error_rate;
            }
        } else {
            st->resolver_degradation_of_signal_error_rate -=
                timestep * (st->resolver_degradation_of_signal_error_rate - 0.0f);
        }

        if (los) {
            angle_is_correct = false;
            ++st->resolver_loss_of_signal_error_cnt;
            st->resolver_loss_of_signal_error_rate -=
                timestep * (st->resolver_loss_of_signal_error_rate - 1.0f);
            if (st->resolver_loss_of_signal_error_rate > st->resolver_LOS_peak_error_rate) {
                st->resolver_LOS_peak_error_rate = st->resolver_loss_of_signal_error_rate;
            }
        } else {
            st->resolver_loss_of_signal_error_rate -=
                timestep * (st->resolver_loss_of_signal_error_rate - 0.0f);
        }

        if (angle_is_correct) {
            st->last_enc_angle = ((float)counts * 360.0f) / 4096.0f;
        }
    }

    return st->last_enc_angle;
}

float encoder_bissc_frame(encoder_bissc_config_t *cfg, const uint8_t frame[8], float now_s) {
    if (cfg == (void *)0 || frame == (void *)0 || cfg->enc_res == 0u || cfg->enc_res > 30u) {
        return 0.0f;
    }

    memcpy(cfg->state.decod_buf, frame, 8u);

    float timestep = now_s - cfg->state.last_update_s;
    if (timestep > 1.0f) {
        timestep = 1.0f;
    }
    cfg->state.last_update_s = now_s;

    uint64_t rx = 0u;
    for (int i = 0; i < 8; i++) {
        rx |= (uint64_t)cfg->state.decod_buf[i] << (56 - 8 * i);
    }

    if (rx == 0u) {
        ++cfg->state.spi_data_error_cnt;
        cfg->state.spi_data_error_rate -= timestep * (cfg->state.spi_data_error_rate - 1.0f);
        return cfg->state.last_enc_angle;
    }

    rx <<= (uint64_t)__builtin_clzll(rx);
    rx &= 0x3FFFFFFFFFFFFFFFu;

    const int nb_bit = 64 - __builtin_clzll(rx);
    const int keep = (int)cfg->enc_res + 10;
    if (nb_bit >= keep) {
        rx >>= (nb_bit - keep);
    }

    const uint8_t crc_rx = (uint8_t)(rx & 0x3Fu);
    const uint32_t data_rx = (uint32_t)((rx >> 6) & ((1u << (cfg->enc_res + 2u)) - 1u));
    cfg->state.spi_val = (data_rx >> 2) & ((1u << cfg->enc_res) - 1u);

    if (encoder_bissc_crc6(cfg->table_crc6n, data_rx) != crc_rx) {
        ++cfg->state.spi_data_error_cnt;
        cfg->state.spi_data_error_rate -= timestep * (cfg->state.spi_data_error_rate - 1.0f);
    } else {
        cfg->state.spi_data_error_rate -= timestep * (cfg->state.spi_data_error_rate - 0.0f);
        cfg->state.last_enc_angle =
            ((float)cfg->state.spi_val * 360.0f) / (float)((1u << cfg->enc_res) - 1u);
    }

    return cfg->state.last_enc_angle;
}

/*
 * enc_mt6835.c:89-129, its routine: the timestep clamped at a second, the six-byte burst read, and
 * then two things that must both hold - the CRC over the three bytes the angle is in against the
 * one that follows, and the sensor's own two status bits being clear. A word that fails either
 * raises the error count and the rate towards one and leaves the angle where it was; a whole one
 * makes the twenty-one bits of the angle, over the sensor's own resolution of two to the
 * twenty-one.
 */
float encoder_mt6835_routine(encoder_mt6835_state_t *st, const encoder_mt6835_port_t *port,
                             float now_s) {
    if (st == (void *)0 || port == (void *)0 || port->read_burst == (void *)0) {
        return 0.0f;
    }

    float timestep = now_s - st->last_update_s;
    if (timestep > 1.0f) {
        timestep = 1.0f;
    }
    st->last_update_s = now_s;

    uint8_t rx[6] = {0};
    if (port->read_burst(port->self, rx) != EDGE_OK) {
        return st->last_enc_angle;
    }

    const uint8_t status = (uint8_t)(rx[4] & 0x07u);
    const uint8_t crc_rx = rx[5];
    const uint8_t crc_calc = encoder_mt6835_crc8(&rx[2], 3);

    const uint32_t angle_raw =
        ((uint32_t)rx[2] << 13) | ((uint32_t)rx[3] << 5) | ((uint32_t)rx[4] >> 3);

    if (crc_rx != crc_calc || status != 0u) {
        st->spi_error_cnt++;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 1.0f);
    } else {
        st->spi_val = angle_raw;
        st->last_enc_angle = ((float)angle_raw * 360.0f) / 2097152.0f; /* MT6835_ANGLE_RES, 2^21 */
        st->spi_error_rate -= timestep * (st->spi_error_rate - 0.0f);
    }

    return st->last_enc_angle;
}
/* driver/spi_bb.c:309-316, the driver's own odd-parity test: the fold, then the low bit of its
 * complement. */
bool encoder_mt6816_parity_ok(uint16_t x) {
    x ^= (uint16_t)(x >> 8);
    x ^= (uint16_t)(x >> 4);
    x ^= (uint16_t)(x >> 2);
    x ^= (uint16_t)(x >> 1);
    return (bool)((~x) & 1u);
}

void encoder_mt6816_begin(encoder_mt6816_state_t *st) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

/*
 * enc_mt6816.c:40-106, its routine: the timestep clamped at a second, the sensor's two registers
 * read and made into one word, and then, if that word's parity holds, the magnet being where it
 * should be - the sensor's own second bit. A magnet that is not raises its own count and rate while
 * the angle holds where it was; a whole word shifts the two flag bits off and gives the angle the
 * low fourteen make, with both rates falling; a word whose parity fails raises the bus error
 * instead.
 */
float encoder_mt6816_routine(encoder_mt6816_state_t *st, const encoder_mt6816_port_t *port,
                             float now_s) {
    if (st == (void *)0 || port == (void *)0 || port->read_registers == (void *)0) {
        return 0.0f;
    }

    float timestep = now_s - st->last_update_s;
    if (timestep > 1.0f) {
        timestep = 1.0f;
    }
    st->last_update_s = now_s;

    uint16_t reg03 = 0u;
    uint16_t reg04 = 0u;
    if (port->read_registers(port->self, &reg03, &reg04) != EDGE_OK) {
        return st->last_enc_angle;
    }

    uint16_t pos = (uint16_t)((reg03 << 8) | reg04);
    st->spi_val = pos;

    if (encoder_mt6816_parity_ok(pos)) {
        if ((pos & 0x0002u) != 0u) { /* MT6816_NO_MAGNET_ERROR_MASK */
            ++st->no_magnet_error_cnt;
            st->no_magnet_error_rate -= timestep * (st->no_magnet_error_rate - 1.0f);
        } else {
            pos = (uint16_t)(pos >> 2);
            st->last_enc_angle = ((float)pos * 360.0f) / 16384.0f;
            st->spi_error_rate -= timestep * (st->spi_error_rate - 0.0f);
            st->no_magnet_error_rate -= timestep * (st->no_magnet_error_rate - 0.0f);
        }
    } else {
        ++st->spi_error_cnt;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 1.0f);
    }

    return st->last_enc_angle;
}

/* ABI Quadrature is the index machine above, whose own state is cleared by encoder_abi_begin. */

/*
 * enc_pwm.c:35-41's clearing and :36-63's callback. The angle is the shorter of the width and the
 * period - a capture wider than its own period is what the reference clamps rather than trusts -
 * over the period, turned a half turn when the input is inverted; the speed between two updates is
 * the shortest way round between the angles (utils_math.h:257-262), and the first two updates only
 * establish it. The ABI timer the reference writes at :61-63 is the caller's, so the count it would
 * write is answered instead.
 */
void encoder_pwm_begin(encoder_pwm_state_t *st, bool update_abi, bool inverted) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
    st->update_abi = update_abi;
    st->inverted = inverted;
}

void encoder_pwm_set_inverted(encoder_pwm_state_t *st, bool inverted) {
    if (st == (void *)0) {
        return;
    }
    st->inverted = inverted;
}

/* utils_math.h:257-262, the reference's own shortest way round. */
static float encoder_pwm_angle_difference(float angle1, float angle2) {
    float difference = angle1 - angle2;
    while (difference < -180.0f) {
        difference += 360.0f;
    }
    while (difference > 180.0f) {
        difference -= 360.0f;
    }
    return difference;
}

void encoder_pwm_update(encoder_pwm_state_t *st, uint32_t width, uint32_t period, float now_s,
                        uint32_t abi_arr, bool *write_abi, uint32_t *abi_count) {
    if (st == (void *)0) {
        return;
    }
    if (write_abi != (void *)0) {
        *write_abi = false;
    }

    st->last_width = width;
    st->last_period = period;
    st->update_cnt++;

    if (period == 0u) {
        /* A capture that reports no period is a port with nothing to say; the reference divides
         * whatever it is given. */
        return;
    }

    const uint32_t shorter = (width < period) ? width : period;
    const float angle_tmp = (float)shorter / (float)period * 360.0f;
    st->angle = st->inverted ? (360.0f - angle_tmp) : angle_tmp;

    if (st->update_cnt > 2u) {
        const float dt = now_s - st->ts_last_s;
        st->speed_per_s = encoder_pwm_angle_difference(st->angle, st->angle_last) / dt;
    }

    st->ts_last_s = now_s;
    st->angle_last = st->angle;

    if (st->update_abi) {
        if (write_abi != (void *)0) {
            *write_abi = true;
        }
        if (abi_count != (void *)0) {
            *abi_count = (uint32_t)(st->angle / 360.0f * (float)abi_arr);
        }
    }
}

float encoder_pwm_read_deg(const encoder_pwm_state_t *st, float now_s) {
    if (st == (void *)0) {
        return 0.0f;
    }

    /* :57-62: the reading interpolates on the speed it last measured, a third of a turn either way.
     */
    float interpol = st->speed_per_s * (now_s - st->ts_last_s);
    if (interpol < -120.0f) {
        interpol = -120.0f;
    }
    if (interpol > 120.0f) {
        interpol = 120.0f;
    }
    return st->angle + interpol;
}

uint32_t encoder_pwm_update_count(const encoder_pwm_state_t *st) {
    return (st != (void *)0) ? st->update_cnt : 0u;
}

/*
 * enc_amt22.c:79-89, its own checksum: two bits at a time over the low fourteen, compared against
 * the two the message carries on top.
 */
bool encoder_amt22_checksum_ok(uint16_t message) {
    uint16_t checksum = 0x3u;
    for (int i = 0; i < 14; i += 2) {
        checksum ^= (uint16_t)((message >> i) & 0x3u);
    }
    return checksum == (uint16_t)(message >> 14);
}

void encoder_amt22_begin(encoder_amt22_state_t *st) {
    if (st == (void *)0) {
        return;
    }
    memset(st, 0, sizeof(*st));
}

/*
 * enc_amt22.c:40-72, its routine: the timestep clamped at a second, the word read and remembered,
 * and - when it is whole - the low fourteen bits over a quarter turn of turns, with the error rate
 * falling towards nought; a word that fails its checksum raises the error count instead, and the
 * rate climbs towards one.
 */
float encoder_amt22_routine(encoder_amt22_state_t *st, const encoder_amt22_port_t *port,
                            float now_s) {
    if (st == (void *)0 || port == (void *)0 || port->read_word == (void *)0) {
        return 0.0f;
    }

    float timestep = now_s - st->last_update_s;
    if (timestep > 1.0f) {
        timestep = 1.0f;
    }
    st->last_update_s = now_s;

    uint16_t pos = 0u;
    if (port->read_word(port->self, &pos) != EDGE_OK) {
        return st->last_enc_angle;
    }
    st->spi_val = pos;

    if (encoder_amt22_checksum_ok(pos)) {
        pos &= 0x3FFFu;
        st->last_enc_angle = ((float)pos * 360.0f) / 16384.0f;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 0.0f);
    } else {
        ++st->spi_error_cnt;
        st->spi_error_rate -= timestep * (st->spi_error_rate - 1.0f);
    }

    return st->last_enc_angle;
} /*
   * enc_abi.c:38-42 - the clearing half of the reference's init, whose other half is the timer's
   * own encoder mode and its EXTI line - and :92-93's reading.
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
