#ifndef APP_THROTTLE_H
#define APP_THROTTLE_H

#include "edge/errors.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Consumer-Defined Ports (Rule: void *self; callbacks take void *self)
 */
typedef struct throttle_input_port {
    edge_status_t (*read_raw)(void *self, float *raw_norm);
    void *self;
} throttle_input_port_t;

typedef struct throttle_output_port {
    edge_status_t (*set_command)(void *self, float cmd);
    void *self;
} throttle_output_port_t;

typedef struct throttle_curve_config {
    float deadband;       /* utils_deadband tres, with max = 1.0 */
    float expo_acc;       /* throttle_exp       - accelerating side */
    float expo_brake;     /* throttle_exp_brake - braking side */
    int expo_mode;        /* throttle_exp_mode  - 0 exp, 1 natural, 2 polynomial, 3 linear */
    float ramp_up_rate;   /* Units per second */
    float ramp_down_rate; /* Units per second */
    float min_out;
    float max_out;
} throttle_curve_config_t;

typedef struct throttle {
    edge_module_t module;

    /* Injected Ports */
    const throttle_input_port_t *input;
    const throttle_output_port_t *output;

    /* Configuration */
    throttle_curve_config_t config;

    /* State */
    float current_output;
    float last_raw_input;
    uint64_t last_poll_ticks;
    float dt_s;
} throttle_t;

float throttle_apply_deadband(float value, float threshold);
float throttle_apply_curve(float raw_in, float curve_acc, float curve_brake, int mode);
float throttle_apply_ramp(float current_val, float target_val, float ramp_up, float ramp_down,
                          float dt);

void throttle_construct(throttle_t *self, uint32_t module_id, uint32_t priority,
                        const throttle_input_port_t *input, const throttle_output_port_t *output,
                        const throttle_curve_config_t *config, float dt_s);

edge_status_t throttle_init(throttle_t *self);
edge_status_t throttle_deinit(throttle_t *self);
edge_status_t throttle_step(throttle_t *self);

edge_module_t *throttle_module(throttle_t *self);
float throttle_get_output(const throttle_t *self);

/*
 * app.c:190-224, app_disable_output and app_is_output_disabled: the gate every application's own
 * output is held behind. A count of milliseconds disables the output for that long, nought clears
 * it at once, and minus one disables it until something clears it - the reference's own three
 * readings of the one argument, with its virtual timer becoming a deadline the caller's clock is
 * compared against.
 */
typedef struct throttle_output_gate {
    bool disabled;
    bool timer_armed;
    uint32_t deadline_ms;
} throttle_output_gate_t;

void throttle_gate_disable(throttle_output_gate_t *gate, int32_t time_ms, uint32_t now_ms);
bool throttle_gate_is_disabled(throttle_output_gate_t *gate, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_THROTTLE_H */
