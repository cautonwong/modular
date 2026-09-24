#ifndef APP_VESC_BMS_H
#define APP_VESC_BMS_H

#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BMS_MAX_CELLS 32u
#define BMS_MAX_TEMPS 8u

typedef struct bms_can_port {
    edge_status_t (*send_can_msg)(void *self, uint32_t id, const uint8_t *data, uint8_t len);
    void *self;
} bms_can_port_t;

typedef struct bms_config {
    uint32_t cell_count;
    float v_cell_min;
    float v_cell_max;
    float temp_max_c;
    float i_in_max_a;
    float i_out_max_a;
    float soc_limit_start;
    float soc_limit_end;
} bms_config_t;

typedef struct bms_values {
    float v_tot;
    float v_charge;
    float i_in;
    float i_in_ic;
    float ah_cnt;
    float wh_cnt;
    float cell_voltages[BMS_MAX_CELLS];
    float temp_sensors[BMS_MAX_TEMPS];
    float temp_ic;
    float temp_hum;
    float hum;
    float soc;
    float soh;
    uint32_t bal_state;
    bool is_charging;
    bool is_balancing;
    uint32_t update_time;
} bms_values_t;

typedef struct vesc_bms_app {
    edge_module_t module;
    bms_can_port_t can_port;
    bms_config_t config;
    bms_values_t values;
    float current_in_limit_max;
    float current_out_limit_max;
    uint32_t fault_code;
} vesc_bms_app_t;

void vesc_bms_construct(vesc_bms_app_t *app, uint32_t module_id, uint32_t priority,
                        const bms_config_t *config, const bms_can_port_t *can_port);
edge_status_t vesc_bms_init(vesc_bms_app_t *app);
edge_status_t vesc_bms_process_can_frame(vesc_bms_app_t *app, uint32_t can_id, const uint8_t *data,
                                         uint8_t len);
void vesc_bms_update_limits(vesc_bms_app_t *app, float *i_in_min, float *i_in_max);
void vesc_bms_get_values(const vesc_bms_app_t *app, bms_values_t *out_val);
edge_module_t *vesc_bms_module(vesc_bms_app_t *app);

#ifdef __cplusplus
}
#endif

#endif /* APP_VESC_BMS_H */
