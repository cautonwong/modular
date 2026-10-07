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
/*
 * enc_as504x.c:32-66's own constants: the read bit, the two diagnostic registers' addresses, the
 * clear-errors one, the mask that drops the parity and error bits, and the three thresholds its
 * connection check, its invalid-word counter and its diagnostic refresh use.
 */
#define AS504X_SPI_READ_BIT 0x4000u
#define AS504X_SPI_EXCLUDE_PARITY_AND_ERROR_BITMASK 0x3FFFu
#define AS504X_SPI_DIAG_ADR 0x3FFDu
#define AS504X_SPI_MAGN_ADR 0x3FFEu
#define AS504X_SPI_CLEAR_ERROR_ADR 0x0001u
#define AS504X_SPI_DIAG_OCF_BIT_POS 8u
#define AS504X_SPI_DIAG_COF_BIT_POS 9u
#define AS504X_SPI_DIAG_COMP_LOW_BIT_POS 10u
#define AS504X_SPI_DIAG_COMP_HIGH_BIT_POS 11u
#define AS504X_CONNECTION_DETERMINATOR_ERROR_THRESHOLD 5u
#define AS504X_DATA_INVALID_THRESHOLD 20000u
#define AS504X_REFRESH_DIAG_AFTER_NSAMPLES 100u

/*
 * enc_as504x.c:47-66's diagnostics, in the reference's own field names, alongside the state that
 * carries them (its AS504x_state): the word last read, the angle, the flags the routine raises, and
 * the three counters.
 */
typedef struct encoder_as504x_diag {
    uint16_t serial_diag_flgs;
    uint16_t serial_magnitude;
    uint16_t serial_error_flags;
    uint16_t AGC_value;
    uint16_t magnitude;
    uint8_t is_OCF;
    uint8_t is_COF;
    uint8_t is_Comp_low;
    uint8_t is_Comp_high;
    uint8_t is_connected;
} encoder_as504x_diag_t;

typedef struct encoder_as504x_state {
    uint32_t spi_val;
    float last_enc_angle;
    float last_update_s;
    float spi_error_rate;
    uint32_t spi_error_cnt;
    uint8_t spi_data_err_raised;
    uint32_t diag_fetch_now_count;
    uint32_t data_last_invalid_counter;
    uint32_t spi_communication_error_count;
    encoder_as504x_diag_t sensor_diag;
} encoder_as504x_state_t;

/*
 * The transfers the family makes, which the reference does over its own bit-banged bus with an
 * instruction's worth of settling between them: what a port answers is one sixteen-bit exchange,
 * including that settling, so that the routine above it is the decisions and not the wiring.
 */
typedef struct encoder_as504x_port {
    void *self;
    bool has_mosi; /* the reference's gate: a MOSI line is what diagnostics are asked over */
    void (*transfer16)(void *self, uint16_t *in_buf, const uint16_t *out_buf, int length);
} encoder_as504x_port_t;

/*
 * enc_mt6816.c:40-106's routine and the state it keeps (MT6816_state): the angle it last read, the
 * word the sensor's two registers made, and the two counts and rates - one for the bus and one for
 * a magnet that is not where it should be.
 */
typedef struct encoder_mt6816_state {
    float last_enc_angle;
    float spi_error_rate;
    float no_magnet_error_rate;
    uint32_t spi_error_cnt;
    uint32_t no_magnet_error_cnt;
    uint16_t spi_val;
    float last_update_s;
} encoder_mt6816_state_t;

/*
 * The reference reads two registers off the sensor and makes one word of them. What a port answers
 * is those two; the parity test between them is the driver's own (driver/spi_bb.c:309-316).
 */
typedef struct encoder_mt6816_port {
    void *self;
    edge_status_t (*read_registers)(void *self, uint16_t *reg03, uint16_t *reg04);
} encoder_mt6816_port_t;

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

/*
 * Hall 6-step Commutation Decoder.
 *
 * This one is not the reference's: its hall path is the six-step layer's own, which reads the pins
 * in mcpwm_read_hall_phase and looks them up in the table that port keeps (bldc_drive), and nothing
 * in the reference's encoder/ directory is a hall family at all. What is here is this port's own
 * addition, and nothing outside these declarations and their tests consumes it - which the port's
 * own rule against dead code reads as a thing to remove rather than keep. It is left standing until
 * that removal can be made with the three places it touches in one change: these declarations, the
 * implementation beside the other families, and the two tests that are its only callers.
 */
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

/*
 * enc_mt6835.c:47-52's burst constants and :89-129's routine, with the state it keeps
 * (MT6835_state): the angle, the word the three bytes make, the error count and rate, and the clock
 * the timestep is taken against.
 */
typedef struct encoder_mt6835_state {
    float last_enc_angle;
    float spi_error_rate;
    uint32_t spi_error_cnt;
    uint32_t spi_val;
    float last_update_s;
} encoder_mt6835_state_t;

/* What a port answers is the six bytes the burst read brings back, command and address included. */
typedef struct encoder_mt6835_port {
    void *self;
    edge_status_t (*read_burst)(void *self, uint8_t rx[6]);
} encoder_mt6835_port_t;

/* AS5047 API */
/*
 * The AS504x family: enc_as504x.c:68-72's initialising, which is a clearing, :150-153's reading -
 * which is the routine and then what it left - and :80-148's routine, whose two paths are whether a
 * MOSI line is there to ask for diagnostics with.
 */
void encoder_as504x_begin(encoder_as504x_state_t *st);
float encoder_as504x_routine(encoder_as504x_state_t *st, const encoder_as504x_port_t *port,
                             float now_s);
float encoder_as504x_read_angle(encoder_as504x_state_t *st, const encoder_as504x_port_t *port,
                                float now_s);
bool encoder_as504x_parity_ok(uint16_t x);

/*
 * The MT6816 family: enc_mt6816.c:29-38's clearing, the driver's own odd-parity test, and :40-106's
 * routine.
 */
void encoder_mt6816_begin(encoder_mt6816_state_t *st);
bool encoder_mt6816_parity_ok(uint16_t x);
float encoder_mt6816_routine(encoder_mt6816_state_t *st, const encoder_mt6816_port_t *port,
                             float now_s);

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

/*
 * The MT6835 family: enc_mt6835.c:40-45's init, which is a clearing, :60-75's own CRC-8 - the
 * polynomial the sensor uses, most significant bit first - and :89-129's routine, whose six-byte
 * burst answers a twenty-one bit angle and two status bits beside it that must be clear.
 */
void encoder_mt6835_begin(encoder_mt6835_state_t *st);
uint8_t encoder_mt6835_crc8(const uint8_t *data, int len);
float encoder_mt6835_routine(encoder_mt6835_state_t *st, const encoder_mt6835_port_t *port,
                             float now_s);

/*
 * The BiSS-C family: enc_bissc.c:64-80's own CRC-6 table - the polynomial 0x43 folded six bits at a
 * time - and :89-110's state, whose frame (:124-180) the reference decodes in the callback of its
 * own asynchronous SPI. What the port answers is the eight bytes that came back, so the routine is
 * that callback's arithmetic without the interrupt.
 */
typedef struct encoder_bissc_state {
    float last_enc_angle;
    float spi_data_error_rate;
    float spi_comm_error_rate;
    uint32_t spi_data_error_cnt;
    uint32_t spi_comm_error_cnt;
    uint32_t spi_val;
    float last_update_s;
    uint8_t decod_buf[8];
} encoder_bissc_state_t;

typedef struct encoder_bissc_config {
    uint32_t enc_res; /* the position's own width, mcconf encoder_bissc_res */
    uint8_t table_crc6n[64];
    encoder_bissc_state_t state;
} encoder_bissc_config_t;

void encoder_bissc_begin(encoder_bissc_config_t *cfg, uint32_t enc_res);
uint8_t encoder_bissc_crc6(const uint8_t table[64], uint32_t data_rx);
float encoder_bissc_frame(encoder_bissc_config_t *cfg, const uint8_t frame[8], float now_s);

/*
 * The AD2S1205 family, a resolver-to-digital converter. Its state (AD2S1205_state) is the word last
 * read, the angle, and the six error rates and counts the resolver carries - the bus's, the three
 * the chip reports (loss of tracking, degradation of signal and loss of signal), the empty packet
 * and the velocity packet - with the six peaks its own reader remembers.
 */
typedef struct encoder_ad2s1205_state {
    uint32_t spi_val;
    float last_enc_angle;
    float last_update_s;
    float spi_error_rate;
    uint32_t spi_error_cnt;
    float resolver_loss_of_tracking_error_rate;
    float resolver_degradation_of_signal_error_rate;
    float resolver_loss_of_signal_error_rate;
    uint32_t resolver_loss_of_tracking_error_cnt;
    uint32_t resolver_degradation_of_signal_error_cnt;
    uint32_t resolver_loss_of_signal_error_cnt;
    float resolver_void_packet_error_rate;
    uint32_t resolver_void_packet_cnt;
    float resolver_vel_packet_error_rate;
    uint32_t resolver_vel_packet_cnt;
    float resolver_LOT_peak_error_rate;
    float resolver_LOS_peak_error_rate;
    float resolver_DOS_peak_error_rate;
    float resolver_SPI_peak_error_rate;
    float resolver_VELread_peak_error_rate;
    float resolver_VOIDspi_peak_error_rate;
} encoder_ad2s1205_state_t;

/* One sixteen-bit frame off the converter, which is what the reference's bit-banged bus answers. */
typedef struct encoder_ad2s1205_port {
    void *self;
    edge_status_t (*read_frame)(void *self, uint16_t *word);
} encoder_ad2s1205_port_t;

/*
 * enc_ad2s1205.c:36-53's initialising, which is a clearing, :66-176's routine, and :178-196's
 * reset, which clears every count, rate and peak it keeps.
 */
void encoder_ad2s1205_begin(encoder_ad2s1205_state_t *st);
float encoder_ad2s1205_routine(encoder_ad2s1205_state_t *st, const encoder_ad2s1205_port_t *port,
                               float now_s);
void encoder_ad2s1205_reset_errors(encoder_ad2s1205_state_t *st);

/*
 * The MA782 family, a magnetic encoder whose angle arrives in a frame the driver asks for and
 * decodes in its callback. Its constants (:39-48), its sub-state (:344-351) and its own error flags
 * are the reference's; so is the state (:355-371), down to the two buffers.
 */
#define MA782_READ_REG_CMD 0x40u
#define MA782_WRITE_REG_CMD 0x80u
#define MA782_REG_FW 0x0Eu
#define MA782_FILTER_WINDOW 6u
#define MA782_RESOLUTION_BITS 9u
#define MA782_MAX_RESOLUTION_BITS 12u

typedef enum encoder_ma782_substate {
    ENCODER_MA782_IDLE = 0,
    ENCODER_MA782_READ_ANGLE_REQ
} encoder_ma782_substate_t;

/* :52-58's flags, which is what the reference's error argument is one bit of. */
enum {
    ENCODER_MA782_CALLBACK_IN_IDLE = 1 << 0,
    ENCODER_MA782_SPI_ERROR = 1 << 1,
    ENCODER_MA782_READ_NOT_IDLE = 1 << 2,
    ENCODER_MA782_SPI_NOT_READY = 1 << 3,
    ENCODER_MA782_WRITE_NOT_IDLE = 1 << 4,
    ENCODER_MA782_WRITE_REG_FAIL = 1 << 5,
    ENCODER_MA782_WRITE_READOUT_FAIL = 1 << 6,
    ENCODER_MA782_ANGLE_NOT_IDLE = 1 << 7,
    ENCODER_MA782_UNKNOWN_STATE = 1 << 8
};

typedef struct encoder_ma782_state {
    float last_enc_angle;
    uint32_t spi_error_cnt;
    float spi_error_rate;
    uint32_t spi_comm_error_cnt;
    float spi_comm_error_rate;
    encoder_ma782_substate_t substate;
    uint16_t rx_data;
    uint16_t tx_data;
    uint32_t start;
    uint32_t error;
    uint32_t error_count;
    uint32_t spi_cnt;
    uint8_t rx_buf[4];
    uint8_t tx_buf[4];
} encoder_ma782_state_t;

/*
 * The family's bus, in the three pieces the reference's own two halves use: the flush its comment
 * describes - the read of the data register that clears a flag a missed reception left set -
 * whether the bus is ready to take another exchange, and the exchange itself, whose answer the
 * callback decodes.
 */
typedef struct encoder_ma782_port {
    void *self;
    void (*flush_rx)(void *self);
    bool (*spi_ready)(void *self);
    void (*start_exchange)(void *self, const uint8_t tx[4], uint8_t rx[4]);
} encoder_ma782_port_t;

void encoder_ma782_begin(encoder_ma782_state_t *st);
void encoder_ma782_error(encoder_ma782_state_t *st, uint32_t flag);
uint16_t encoder_ma782_resolution_mask(void);
bool encoder_ma782_read_angle(encoder_ma782_state_t *st, const encoder_ma782_port_t *port);
bool encoder_ma782_routine(encoder_ma782_state_t *st, const encoder_ma782_port_t *port);
float encoder_ma782_read_angle_finish(encoder_ma782_state_t *st);

/*
 * The TS5700N8501 family, whose frames arrive over a serial line rather than a register: the reply
 * is eleven bytes whose last is the exclusive-or of the ten before it (:168-206), and the position
 * is the three after the first, little-endian, over the sensor's own 131072 counts of a turn. Its
 * eight status bytes are kept verbatim, and the run's own stop, running and reset flags are the
 * thread's - which is why they are the caller's here.
 */
#define ENCODER_TS5700_REPLY_LEN 11u
#define ENCODER_TS5700_COUNTS_PER_TURN 131072.0f

typedef struct encoder_ts5700_state {
    float last_enc_angle;
    uint32_t spi_val;
    float spi_error_rate;
    uint32_t spi_error_cnt;
    uint8_t raw_status[8];
} encoder_ts5700_state_t;

void encoder_ts5700_begin(encoder_ts5700_state_t *st);
float encoder_ts5700_decode(encoder_ts5700_state_t *st,
                            const uint8_t reply[ENCODER_TS5700_REPLY_LEN], float timestep);

/*
 * The AS5x47U family: a sensor whose every frame carries a CRC-8 the reference keeps a table for
 * (enc_as5x47u.c:90-115, the polynomial 0x1D) and whose reads each have an expected value checked
 * in at :64-68. Its reading of a position frame is the low fourteen bits over a quarter turn
 * (:328-333).
 */
#define AS5X47U_SPI_READ_BIT 0x4000u
#define AS5X47U_SPI_EXCLUDE_PARITY_AND_ERROR_BITMASK 0x3FFFu
#define AS5X47U_SPI_ERRFL_ADR 0x0001u
#define AS5X47U_SPI_DIAG_ADR 0x3FF5u
#define AS5X47U_SPI_MAGN_ADR 0x3FFDu
#define AS5X47U_SPI_AGC_ADR 0x3FF9u
#define AS5X47U_SPI_POS_ADR 0x3FFFu
#define AS5X47U_SPI_READ_ERRFL_CRC 0x06u
#define AS5X47U_SPI_READ_DIAG_CRC 0x6Fu
#define AS5X47U_SPI_READ_MAGN_CRC 0x87u
#define AS5X47U_SPI_READ_AGC_CRC 0xF3u
#define AS5X47U_SPI_READ_POS_CRC 0xBDu

/* The seed the reference's own transmitting CRC is made with, and the complement it ends on (:356).
 */
#define AS5X47U_SPI_TX_CRC_SEED 0xC4u

/* The eight states the family asks its frames in (enc_as5x47u.c:71-82). */
typedef enum encoder_as5x47u_seq {
    ENCODER_AS5X47U_SEQ_TX_MAG_RX_POS = 0,
    ENCODER_AS5X47U_SEQ_TX_POS_RX_MAG,
    ENCODER_AS5X47U_SEQ_TX_AGC_RX_POS,
    ENCODER_AS5X47U_SEQ_TX_POS_RX_AGC,
    ENCODER_AS5X47U_SEQ_TX_DIAG_RX_POS,
    ENCODER_AS5X47U_SEQ_TX_POS_RX_DIAG,
    ENCODER_AS5X47U_SEQ_TX_ERRFL_RX_POS,
    ENCODER_AS5X47U_SEQ_TX_POS_RX_ERRFL,
    ENCODER_AS5X47U_SEQ_PREV_ERR
} encoder_as5x47u_seq_t;

/* :38-45's diagnostics, in the reference's own field names, and the two frame flags beside them. */
typedef struct encoder_as5x47u_diag {
    uint8_t is_error;
    uint8_t is_crc_error;
    uint8_t is_wdt;
    uint8_t is_mag_half;
    uint8_t is_broken_hall;
    uint8_t is_COF;
    uint8_t is_Comp_low;
    uint8_t is_Comp_high;
    uint8_t is_connected;
    uint16_t serial_magnitude;
    uint16_t magnitude;
    uint16_t serial_AGC_value;
    uint8_t AGC_value;
    uint16_t serial_diag_flgs;
    uint16_t serial_errfl;
    uint32_t spi_communication_error_count;
    float spi_error_rate;
    uint32_t spi_error_cnt;
} encoder_as5x47u_diag_t;

typedef struct encoder_as5x47u_state {
    uint16_t spi_val;
    float last_enc_angle;
    float last_update_s;
    uint8_t table_crc8[256];
    encoder_as5x47u_seq_t spi_seq;
    encoder_as5x47u_diag_t sensor_diag;
} encoder_as5x47u_state_t;

void encoder_as5x47u_begin(encoder_as5x47u_state_t *st);
void encoder_as5x47u_build_crc_table(uint8_t table[256]);
uint8_t encoder_as5x47u_crc8(const uint8_t table[256], const uint8_t *data, size_t len,
                             uint8_t initial);
uint8_t encoder_as5x47u_transmit_crc(const uint8_t table[256], uint16_t tx_data);
void encoder_as5x47u_process_pos(encoder_as5x47u_state_t *st, uint16_t pos_data);

/*
 * The TLE5012 family, whose answers carry a safety word beside their data. Its three masks, its own
 * CRC (the polynomial 0x1D but seeded with 0xFF rather than the family next door's 0xC4), its error
 * verdicts (:144-151) and the count its command word's last four bits carry (:273-281) are here.
 */
typedef enum encoder_tle5012_error {
    ENCODER_TLE5012_NO_ERROR = 0x00,
    ENCODER_TLE5012_SYSTEM_ERROR = 0x01,
    ENCODER_TLE5012_INTERFACE_ACCESS_ERROR = 0x02,
    ENCODER_TLE5012_INVALID_ANGLE_ERROR = 0x04,
    ENCODER_TLE5012_ANGLE_SPEED_ERROR = 0x08,
    ENCODER_TLE5012_CRC_ERROR = 0xFF
} encoder_tle5012_error_t;

#define TLE5012_SYSTEM_ERROR_MASK 0x4000u
#define TLE5012_INTERFACE_ERROR_MASK 0x2000u
#define TLE5012_INV_ANGLE_ERROR_MASK 0x1000u
#define TLE5012_CRC_POLYNOMIAL 0x1Du
#define TLE5012_CRC_SEED 0xFFu
#define TLE5012_COMMAND_SAFETY_WORD 0x0001u
#define TLE5012_COMMAND_NO_SAFETY_WORD 0x0000u

/* :304-324: the seed, the byte in, eight shifts, and the complement it ends on. */
uint8_t encoder_tle5012_crc8(const uint8_t *data, uint8_t length);

/*
 * :82 and :328-380: the safety word's three masks - each of them inverted, so that a bit which is
 * clear is the fault rather than one that is set - and then the checksum over the command word and
 * every data word, whose mismatch is the CRC verdict.
 */
encoder_tle5012_error_t encoder_tle5012_check_safety(uint16_t command, uint16_t safety_word,
                                                     const uint16_t *read_words, uint16_t length);

/* :221: the position over two to the fifteenth of a turn. */
float encoder_tle5012_pos_to_deg(uint16_t pos);

/*
 * The core the families are read through: the type the composition root chose
 * (encoder_datatype.h:30-47), and the bookkeeping the reference keeps around it - which family's
 * reader to call, the multiturn of the one that has it, the index flag of the two that have that,
 * and the angle a caller can set.
 */
typedef enum encoder_core_type {
    ENCODER_CORE_TYPE_NONE = 0,
    ENCODER_CORE_TYPE_AS504X,
    ENCODER_CORE_TYPE_MT6816,
    ENCODER_CORE_TYPE_TLE5012,
    ENCODER_CORE_TYPE_AD2S1205_SPI,
    ENCODER_CORE_TYPE_SINCOS,
    ENCODER_CORE_TYPE_TS5700N8501,
    ENCODER_CORE_TYPE_ABI,
    ENCODER_CORE_TYPE_AS5X47U,
    ENCODER_CORE_TYPE_BISSC,
    ENCODER_CORE_TYPE_CUSTOM,
    ENCODER_CORE_TYPE_PWM,
    ENCODER_CORE_TYPE_PWM_ABI,
    ENCODER_CORE_TYPE_MA782,
    ENCODER_CORE_TYPE_AMT22,
    ENCODER_CORE_TYPE_MT6835
} encoder_core_type_t;

/*
 * What the core cannot do itself, because it is the hardware's or another family's:
 *
 *   set_abi_count        :141-143's two writes to the ABI timer's counter and its index flag
 *   set_custom_deg       :145's stored position for a sensor that is not one of the others
 *   abi_index_found      :155-158's own flag
 *   pwm_abi_ready        :159-165: after two updates the flag is set and the capture is let go
 *   ts_multiturn          :132-140's ABM count, and the two resets that go with it
 */
typedef struct encoder_core_port {
    void *self;
    void (*set_abi_deg)(void *self, float deg);
    void (*set_custom_deg)(void *self, float deg);
    bool (*abi_index_found)(void *self);
    bool (*pwm_abi_ready)(void *self);
    float (*ts_multiturn)(void *self);
    void (*ts_reset_multiturn)(void *self);
    void (*ts_reset_errors)(void *self);
} encoder_core_port_t;

typedef struct encoder_core {
    encoder_core_type_t type;
    /* The family's own reader, which the caller wires to whichever family it constructed. */
    float (*read_deg)(void *self);
    void *read_self;
    float custom_deg;
} encoder_core_t;

/* :68-267's dispatch is the composition root's; what is here is the bookkeeping around it. */
void encoder_core_begin(encoder_core_t *c, encoder_core_type_t type, float (*read_deg)(void *self),
                        void *read_self);
float encoder_core_read_deg(const encoder_core_t *c);
float encoder_core_read_deg_multiturn(encoder_core_t *c, const encoder_core_port_t *port);
void encoder_core_set_deg(encoder_core_t *c, const encoder_core_port_t *port, float deg);
encoder_core_type_t encoder_core_is_configured(const encoder_core_t *c);
bool encoder_core_index_found(encoder_core_t *c, const encoder_core_port_t *port);
void encoder_core_reset_multiturn(const encoder_core_t *c, const encoder_core_port_t *port);
void encoder_core_reset_errors(const encoder_core_t *c, const encoder_core_port_t *port);

float encoder_core_abi_deg_to_count(float deg, float counts);

/*
 * encoder.c:186-…, encoder_check_faults: the faults the encoder's own error rates and flags stand
 * for, gated on the encoder actually being the one the motor is commuting from. The verdicts are
 * the reference's own; raising the fault belongs to the product, which is where its fault codes
 * live.
 */
typedef enum encoder_fault {
    ENCODER_FAULT_NONE = 0,
    ENCODER_FAULT_SPI,
    ENCODER_FAULT_NO_MAGNET,
    ENCODER_FAULT_MAGNET_TOO_STRONG
} encoder_fault_t;

/* The reference's own threshold on every error rate it reads before it faults (:195 and beside) */
#define ENCODER_FAULT_ERROR_RATE_THRESHOLD 0.05f

/* sensor_port_mode (datatypes.h:206-226), the ones this policy reads by name. */
#define ENCODER_PORT_MODE_AS5047_SPI 2u
#define ENCODER_PORT_MODE_MT6816_SPI_HW 7u
#define ENCODER_PORT_MODE_MT6835_SPI_HW 17u

/*
 * What the policy reads of each family: the bus rate of the ones that have one, the magnet rate of
 * the one that reports it that way, and the three diagnostic flags the AS504x carries beside its
 * word.
 */
typedef struct encoder_fault_inputs {
    float as504x_spi_error_rate;
    bool as504x_has_mosi;
    bool as504x_is_connected;
    bool as504x_is_comp_high;
    bool as504x_is_comp_low;
    float mt6816_no_magnet_error_rate;
    float mt6835_spi_error_rate;
} encoder_fault_inputs_t;

encoder_fault_t encoder_core_check_faults(bool encoder_in_use, uint8_t sensor_port_mode,
                                          const encoder_fault_inputs_t *in);

/*
 * The frame the state answered with, decoded; what comes back is the message to request next, whose
 * own two bytes and CRC the caller sends - which keeps this family's arithmetic free of the bus.
 */
uint16_t encoder_as5x47u_callback(encoder_as5x47u_state_t *st, const uint8_t rx[3], float now_s);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_ENCODER_H */
