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
    float deadband;
    float expo;
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

float throttle_apply_curve(float raw_in, float deadband, float expo);
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

#ifdef __cplusplus
}
#endif

#endif /* APP_THROTTLE_H */
