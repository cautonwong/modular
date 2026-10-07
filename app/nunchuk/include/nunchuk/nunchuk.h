#ifndef NUNCHUK_H
#define NUNCHUK_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * datatypes.h:727-731's values in its own order, under names this application owns: the generated
 * copy lives in another application and one may not depend on another (D30), the same call the PPM,
 * PAS and FOC applications make.
 */
typedef enum nunchuk_mode {
    NUNCHUK_MODE_NONE = 0,
    NUNCHUK_MODE_CURRENT,
    NUNCHUK_MODE_CURRENT_NOREV,
    NUNCHUK_MODE_CURRENT_BIDIRECTIONAL,
} nunchuk_mode_t;

/* datatypes.h:616-619: three modes, where this port's curve has a fourth branch for everything
 * else. */
typedef enum nunchuk_curve_mode {
    NUNCHUK_CURVE_EXPO = 0,
    NUNCHUK_CURVE_NATURAL,
    NUNCHUK_CURVE_POLY,
} nunchuk_curve_mode_t;

/* datatypes.h:733-750, the reference's own fields and their types. */
typedef struct nunchuk_config {
    nunchuk_mode_t ctrl_type;
    float hyst;
    float ramp_time_pos;
    float ramp_time_neg;
    float stick_erpm_per_s_in_cc;
    float throttle_exp;
    float throttle_exp_brake;
    nunchuk_curve_mode_t throttle_exp_mode;
    bool multi_esc;
    bool tc;
    float tc_max_diff;
    bool use_smart_rev;
    float smart_rev_max_duty;
    float smart_rev_ramp_time;
    float coast_brake_level;
    float coast_brake_ramp_time;
} nunchuk_config_t;

/* datatypes.h:1265-1275, the state the controller reports. */
typedef struct nunchuk_data {
    uint8_t js_x;
    uint8_t js_y;
    uint8_t acc_x;
    uint8_t acc_y;
    uint8_t acc_z;
    bool bt_z;
    bool bt_c;
    bool rev_has_state;
    bool is_rev;
} nunchuk_data_t;

/*
 * applications/app_nunchuk.c:132-206's transfers, at the byte level: the application owns the
 * protocol
 * - which register pairs are written, the pause before the read, and the frame's own validity - and
 * this port is the bus. The delay is the port's because the reference sleeps inside its sequence.
 *
 * The bus is optional where the two ports beside it are not: a product with no I2C - this port's
 * host is one - has no controller to poll, and the reference's own output thread runs regardless of
 * whether a frame has ever arrived, so the policy half is what such a product drives.
 */
typedef struct nunchuk_port {
    void *self;
    edge_status_t (*transfer)(void *self, const uint8_t *tx, size_t tx_len, uint8_t *rx,
                              size_t rx_len);
    edge_status_t (*delay_ms)(void *self, uint32_t ms);
    edge_status_t (*now_ms)(void *self, uint32_t *ms);
    /*
     * applications/app_nunchuk.c:384-415's CAN aggregation, over the peers the product keeps: the
     * lowest magnitude of their speeds, and the highest magnitude of their currents - taken
     * directional, which the reference does by flipping a negative-duty peer's current - and of
     * their duties.
     */
    edge_status_t (*read_peer_aggregate)(void *self, float *rpm_lowest, float *current_highest,
                                         float *duty_highest_abs);
} nunchuk_port_t;

/*
 * What the reference's output thread reads from the motor interface while deciding - the readings
 * and the limits - and what it decides to do, as a value the product applies
 * (app_nunchuk.c:218-527).
 */
typedef struct nunchuk_policy_in {
    float rpm_now;
    float lo_current_max;
    float lo_current_min;
    float l_current_max;
    float l_current_max_scale;
    float l_current_min;
    float l_current_min_scale;
    float l_min_duty;
    float cc_min_current;
    float s_pid_min_erpm;
    float duty_now;
    float current_now;
} nunchuk_policy_in_t;

typedef enum nunchuk_command_kind {
    NUNCHUK_CMD_NONE = 0, /* the reference's own early continues: nothing is commanded this pass */
    NUNCHUK_CMD_BRAKE,
    NUNCHUK_CMD_CURRENT,
    NUNCHUK_CMD_DUTY,
    NUNCHUK_CMD_PID_SPEED,
} nunchuk_command_kind_t;

typedef struct nunchuk_command {
    nunchuk_command_kind_t kind;
    float current;
    float duty;
    float pid_rpm;
} nunchuk_command_t;

typedef struct nunchuk_app {
    edge_module_t module;
    nunchuk_config_t config;
    nunchuk_port_t port;

    nunchuk_data_t data;
    /* app_nunchuk.c:207-217: the last frame, so that an unchanged one is not reported again. */
    uint8_t last_frame[6];
    bool frame_seen;
    /* app_nunchuk.c:203-211's error: 0 is fine, 1 a timeout, 2 a bus failure. */
    int error;
    /* app_nunchuk.c:270-330's own memory of what the sticks have been doing. */
    bool is_reverse;
    bool was_z;
    bool was_pid;
    float pid_rpm;
    bool coast_brake_prev;
    float prev_current;
    float duty_rev;
    bool was_duty_control;
    /* app_nunchuk.c:191-198: when the controller last reported, and whether its thread is running.
     */
    uint32_t last_update_ms;
    bool have_update;
    bool active;
} nunchuk_app_t;

void nunchuk_construct(nunchuk_app_t *app, uint32_t module_id, uint32_t priority,
                       const nunchuk_config_t *config, const nunchuk_port_t *port);
edge_status_t nunchuk_init(nunchuk_app_t *app);

/*
 * applications/app_nunchuk.c:243-256, the unpacking of one six-byte frame, including the two bits
 * of each accelerometer axis that live in the last byte and the two buttons that are active low. It
 * returns false when the frame is identical to the last one, which the reference also treats as
 * nothing new to report.
 */
bool nunchuk_decode_frame(nunchuk_app_t *app, const uint8_t frame[6], nunchuk_data_t *out);

/* application/app_nunchuk.c:64-71: the stick as the reference reports it, 128 being the centre. */
float nunchuk_get_decoded_x(const nunchuk_app_t *app);
float nunchuk_get_decoded_y(const nunchuk_app_t *app);
bool nunchuk_get_bt_c(const nunchuk_app_t *app);
bool nunchuk_get_bt_z(const nunchuk_app_t *app);
bool nunchuk_get_is_rev(const nunchuk_app_t *app);
bool nunchuk_has_update(const nunchuk_app_t *app);
uint32_t nunchuk_get_update_age_ms(const nunchuk_app_t *app);

/* applications/app_nunchuk.c:104-121: a frame arrived, so the output side is live again. */
void nunchuk_update_data(nunchuk_app_t *app, const nunchuk_data_t *data);

/* applications/app_nunchuk.c:218-527: the decision half, as a value for the product to apply. */
nunchuk_command_t nunchuk_policy(nunchuk_app_t *app, const nunchuk_policy_in_t *in, float dt);

int nunchuk_get_error(const nunchuk_app_t *app);
void nunchuk_set_error(nunchuk_app_t *app, int error);
bool nunchuk_is_active(const nunchuk_app_t *app);
edge_module_t *nunchuk_module(nunchuk_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* NUNCHUK_H */
