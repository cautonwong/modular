#ifndef ZMK_POINTING_PROCESSORS_H
#define ZMK_POINTING_PROCESSORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edge/errors.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_pointing_proc_sink_if {
    void *self;
    edge_status_t (*set_temp_layer)(void *self, uint8_t layer, bool active);
    edge_status_t (*forward_motion)(void *self, int16_t dx, int16_t dy, int16_t dwheel);
} zmk_pointing_proc_sink_if_t;

typedef struct zmk_scaler_config {
    uint16_t mul;
    uint16_t div;
} zmk_scaler_config_t;

typedef struct zmk_temp_layer_config {
    uint8_t toggle_layer;
    uint32_t timeout_ms;
    bool enabled;
} zmk_temp_layer_config_t;

typedef struct zmk_pointing_processors_app {
    edge_module_t module;
    zmk_pointing_proc_sink_if_t sink;
    zmk_scaler_config_t scaler;
    zmk_temp_layer_config_t temp_layer;
    int16_t remainder_x;
    int16_t remainder_y;
    int16_t remainder_wheel;
    bool layer_active;
    uint32_t layer_disable_deadline_ms;
} zmk_pointing_processors_app_t;

void zmk_pointing_processors_construct(zmk_pointing_processors_app_t *app, uint32_t module_id,
                                       uint8_t priority, const zmk_pointing_proc_sink_if_t *sink);

edge_status_t zmk_pointing_processors_init(zmk_pointing_processors_app_t *app);

void zmk_pointing_processors_set_scaler(zmk_pointing_processors_app_t *app, uint16_t mul,
                                        uint16_t div);

void zmk_pointing_processors_set_temp_layer(zmk_pointing_processors_app_t *app, uint8_t layer,
                                            uint32_t timeout_ms);

edge_status_t zmk_pointing_processors_process_motion(zmk_pointing_processors_app_t *app,
                                                     int16_t raw_dx, int16_t raw_dy,
                                                     int16_t raw_dwheel, uint32_t timestamp_ms);

edge_status_t zmk_pointing_processors_process_tick(zmk_pointing_processors_app_t *app,
                                                   uint32_t current_time_ms);

#ifdef __cplusplus
}
#endif

#endif
