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

/* MT6816 Registers & Constants */
#define MT6816_REG_ANGLE1 0x03
#define MT6816_REG_ANGLE2 0x04
#define MT6816_CPR 16384u

typedef edge_status_t (*encoder_spi_transfer_fn)(void *ctx, uint16_t tx_val, uint16_t *rx_val);

/* AS5047 SPI Magnetic Encoder */
typedef struct encoder_as5047 {
    encoder_spi_transfer_fn spi_transfer;
    void *spi_ctx;
    uint16_t last_angle_raw;
    uint32_t parity_errors;
} encoder_as5047_t;

/* MT6816 SPI Magnetic Encoder */
typedef struct encoder_mt6816 {
    encoder_spi_transfer_fn spi_transfer;
    void *spi_ctx;
    uint16_t last_angle_raw;
    bool no_magnet;
} encoder_mt6816_t;

/*
 * enc_abi.c:100-124, the index pulse's own machine, and the state it keeps
 * (encoder_datatype.h:153-158): whether the index has been found, the count it was last seen at,
 * how many pulses in a row have been implausible, and how many there have been at all.
 */
typedef struct encoder_abi_state {
    bool index_found;
    uint32_t cnt_at_ind_last;
    int bad_pulses;
    uint32_t index_pulse_cnt;
} encoder_abi_state_t;

/* Its configuration is the timer's own reload, which is the counts in one revolution. */
typedef struct encoder_abi_config {
    uint32_t counts;
    encoder_abi_state_t state;
} encoder_abi_config_t;

/*
 * What the machine reads and writes that belongs to the hardware: the counter the timer holds, and
 * the index pin, whose level once a few instructions have settled is the reference's own noise
 * test. Writing the counter back is the caller's, because it is the timer's register.
 */
typedef struct encoder_abi_port {
    void *self;
    uint32_t (*read_count)(void *self);
    void (*write_count)(void *self, uint32_t count);
    bool (*read_index_high)(void *self);
} encoder_abi_port_t;

/*
 * enc_sincos.c:44-101, enc_sincos_read_deg: the analog resolver. The reference's ENCSINCOS_state
 * (encoder_datatype.h:177-186) is the two filters, the angle it last reported, when it last ran and
 * the two error rates and counts its amplitude window keeps.
 */
typedef struct encoder_sincos_state {
    uint32_t signal_below_min_error_cnt;
    uint32_t signal_above_max_error_cnt;
    float signal_low_error_rate;
    float signal_above_max_error_rate;
    float last_enc_angle;
    float sin_filter;
    float cos_filter;
    float last_update_s;
} encoder_sincos_state_t;

/*
 * Its configuration (encoder_datatype.h:188-204). The two gains are stored as one over the
 * amplitude - the reference's own comment says so, which is why reading never divides by one - and
 * the phase correction is kept both as the angle and as its sine and cosine.
 */
typedef struct encoder_sincos_config {
    float sin_gain;
    float cos_gain;
    float sin_offset;
    float cos_offset;
    float filter_constant;
    float phase_correction_deg;
    float sin_phase;
    float cos_phase;
    float ratio;
    float delay_comp_sign;
    encoder_sincos_state_t state;
} encoder_sincos_config_t;

/*
 * The two things the family reads that are not the sensor itself, which the reference reaches
 * through mc_interface and its timer: the speed the delay compensation is built from, and the clock
 * its timestep is measured against, in seconds.
 */
typedef struct encoder_sincos_port {
    void *self;
    float (*read_rpm)(void *self);
    float (*now_seconds)(void *self);
} encoder_sincos_port_t;

/* Hall 6-step Commutation Decoder */
typedef struct encoder_hall {
    uint8_t hall_tab[8];
    uint8_t last_hall_state;
    float last_angle_rad;
} encoder_hall_t;

/*
 * enc_pwm.c:36-63's machine, which the reference keeps in file-scope statics: the width and the
 * period the capture callback just saw, how many updates there have been, the angle those make, the
 * one before it and the speed between them, and the two settings it carries.
 */
typedef struct encoder_pwm_state {
    uint32_t last_width;
    uint32_t last_period;
    uint32_t update_cnt;
    float angle;
    float angle_last;
    float speed_per_s;
    float ts_last_s;
    bool update_abi;
    bool inverted;
} encoder_pwm_state_t;

/*
 * enc_amt22.c:40-72's routine and the state it keeps (enc_amt22.h's AS504x_state): the angle it
 * last read, the word it read, and the error count and rate the checksum feeds.
 */
typedef struct encoder_amt22_state {
    float last_enc_angle;
    float spi_error_rate;
    uint32_t spi_error_cnt;
    uint16_t spi_val;
    float last_update_s;
} encoder_amt22_state_t;

/*
 * The one word the AMT22 answers, which the reference takes over its own bit-banged bus in two
 * eight-bit transfers: what a port hands over is the sixteen bits those make.
 */
typedef struct encoder_amt22_port {
    void *self;
    edge_status_t (*read_word)(void *self, uint16_t *word);
} encoder_amt22_port_t;

/* AS5047 API */
void encoder_as5047_construct(encoder_as5047_t *self, encoder_spi_transfer_fn spi_transfer,
                              void *spi_ctx);
edge_status_t encoder_as5047_init(encoder_as5047_t *self);
edge_status_t encoder_as5047_read_angle_raw(encoder_as5047_t *self, uint16_t *raw_angle);
edge_status_t encoder_as5047_read_angle_rad(encoder_as5047_t *self, float *angle_rad);
edge_status_t encoder_as5047_read_diag(encoder_as5047_t *self, uint16_t *diag_val);
bool encoder_as5047_check_parity(uint16_t val);

/* MT6816 API */
void encoder_mt6816_construct(encoder_mt6816_t *self, encoder_spi_transfer_fn spi_transfer,
                              void *spi_ctx);
edge_status_t encoder_mt6816_init(encoder_mt6816_t *self);
edge_status_t encoder_mt6816_read_angle_raw(encoder_mt6816_t *self, uint16_t *raw_angle);
edge_status_t encoder_mt6816_read_angle_rad(encoder_mt6816_t *self, float *angle_rad);

/*
 * ABI API: enc_abi.c:38-42, whose init clears its state, :92-93's own reading, and :100-124's index
 * machine. The rest of the reference's init is the timer's and the EXTI line's, which are the
 * product's to wire.
 */
void encoder_abi_begin(encoder_abi_config_t *cfg);
float encoder_abi_read_deg(uint32_t count, uint32_t counts);
void encoder_abi_index_pulse(encoder_abi_config_t *cfg, const encoder_abi_port_t *port);

/* SinCos API: enc_sincos.c:35-42, whose init and deinit are both a clearing, and :44-101. */
void encoder_sincos_begin(encoder_sincos_config_t *cfg);
float encoder_sincos_read_deg(encoder_sincos_config_t *cfg, const encoder_sincos_port_t *port,
                              float sin_volts, float cos_volts);

/* Hall API */
void encoder_hall_construct(encoder_hall_t *self, const uint8_t *custom_tab);
edge_status_t encoder_hall_init(encoder_hall_t *self);
edge_status_t encoder_hall_update(encoder_hall_t *self, uint8_t hall_state, float *out_angle_rad);

/*
 * The PWM-input family: enc_pwm.c:47-63's callback, whose angle is the shorter of the width and the
 * period over the period, turned a half turn when the input is inverted, with the speed between two
 * updates for the interpolation the reading adds (:36-41 is the clearing its init does). The write
 * of the ABI timer's counter (:61-63) is the caller's, so update() answers with the count to write
 * and only when the run was told to.
 */
void encoder_pwm_begin(encoder_pwm_state_t *st, bool update_abi, bool inverted);
void encoder_pwm_set_inverted(encoder_pwm_state_t *st, bool inverted);
void encoder_pwm_update(encoder_pwm_state_t *st, uint32_t width, uint32_t period, float now_s,
                        uint32_t abi_arr, bool *write_abi, uint32_t *abi_count);
float encoder_pwm_read_deg(const encoder_pwm_state_t *st, float now_s);
uint32_t encoder_pwm_update_count(const encoder_pwm_state_t *st);

/*
 * The AMT22 family: enc_amt22.c:40-72's routine and :74-77's reading, with its checksum (:79-89)
 * exposed because that is what the reference's own static is and what its test drives.
 */
void encoder_amt22_begin(encoder_amt22_state_t *st);
float encoder_amt22_routine(encoder_amt22_state_t *st, const encoder_amt22_port_t *port,
                            float now_s);
bool encoder_amt22_checksum_ok(uint16_t message);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_ENCODER_H */
