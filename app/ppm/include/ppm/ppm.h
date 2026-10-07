#ifndef PPM_H
#define PPM_H

#include <stdbool.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"
#include "edge/modules.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * datatypes.h:628-641's ppm_control_type, in its own order and with its own values - the reference
 * stores a mode by value, so the numbers are part of what is being ported and not a detail of this
 * header. Its PID_POSITION_180 and PID_POSITION_360 are the two position modes, and PID is the one
 * that reads a speed.
 */
typedef enum ppm_control_mode {
    PPM_MODE_NONE = 0,
    PPM_MODE_CURRENT,
    PPM_MODE_CURRENT_NOREV,
    PPM_MODE_CURRENT_NOREV_BRAKE,
    PPM_MODE_DUTY,
    PPM_MODE_DUTY_NOREV,
    PPM_MODE_PID,
    PPM_MODE_PID_NOREV,
    PPM_MODE_CURRENT_BRAKE_REV_HYST,
    PPM_MODE_CURRENT_SMART_REV,
    PPM_MODE_PID_POSITION_180,
    PPM_MODE_PID_POSITION_360,
} ppm_control_mode_t;

typedef struct ppm_config {
    ppm_control_mode_t mode;
    float pulse_min_us;
    float pulse_max_us;
    float pulse_center_us;
    float timeout_s;
    bool safe_start;
    /*
     * applications/app_ppm.c:190-199: the chain the reference runs the decoded value through before
     * it becomes a command - a deadband that rescales rather than zeroes, the throttle curve, and a
     * ramp with a time of its own for each direction. All three default to no change, which is what
     * this application did before they were here.
     */
    float hyst;
    float throttle_exp;
    float throttle_exp_brake;
    int throttle_exp_mode;
    float ramp_time_pos;
    float ramp_time_neg;
} ppm_config_t;

typedef struct ppm_receiver_port {
    void *self;
    edge_status_t (*read_pulse_us)(void *self, float *pulse_us);
    bool (*is_signal_present)(void *self);
} ppm_receiver_port_t;

typedef struct ppm_app {
    edge_module_t module;
    ppm_config_t config;
    ppm_receiver_port_t receiver_port;
    float last_pulse_us;
    float time_since_last_pulse_s;
    float output_norm; /* -1.0 to 1.0 */
    bool safe_start_unlocked;
    bool signal_lost;
    /* applications/app_ppm.c:55-56 and :93-97, the detaching flag and the value it substitutes. */
    bool detached;
    float override_norm;
    /* applications/app_ppm.c:199's own ramp state, which survives between updates. */
    float output_ramp;
    /* applications/app_ppm.c:53, input_val: what the decoded level reports, before the group of
     * modes below re-maps it for the command. */
    float decoded_norm;
} ppm_app_t;

void ppm_construct(ppm_app_t *app, uint32_t module_id, uint32_t priority,
                   const ppm_config_t *config, const ppm_receiver_port_t *receiver_port);
edge_status_t ppm_init(ppm_app_t *app);
edge_status_t ppm_update(ppm_app_t *app, float dt);
float ppm_get_output(const ppm_app_t *app);
/*
 * applications/app_ppm.c:89, app_ppm_get_decoded_level: the value as decoded, in [-1, 1], which is
 * not what get_output above reports for the four modes that re-map it onto [0, 1].
 */
float ppm_get_decoded_level(const ppm_app_t *app);
/* Last accepted pulse width in microseconds (reference: servodec_get_last_pulse_len). */
float ppm_get_last_pulse_us(const ppm_app_t *app);
bool ppm_is_safe(const ppm_app_t *app);

/*
 * applications/app_ppm.c:93-99, the detaching flag and the override that replaces the decoded value
 * when it is set. The reference maps the override exactly as it maps a decoded one - it substitutes
 * it in [-1, 1] and truncates nothing - so neither does this.
 */
void ppm_detach(ppm_app_t *app, bool detach);
bool ppm_is_detached(const ppm_app_t *app);
void ppm_override(ppm_app_t *app, float val);
float ppm_get_override(const ppm_app_t *app);
edge_module_t *ppm_module(ppm_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* PPM_H */
