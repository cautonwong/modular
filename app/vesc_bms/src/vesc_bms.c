#include "vesc_bms/vesc_bms.h"
#include <string.h>

static edge_status_t vesc_bms_poll(edge_module_t *mod) {
    if (!mod || !mod->private_data) {
        return EDGE_EINVAL;
    }
    vesc_bms_app_t *app = (vesc_bms_app_t *)mod->private_data;

    /* Calculate SOC based on average cell voltage */
    if (app->config.cell_count > 0 && app->config.v_cell_max > app->config.v_cell_min) {
        float avg_cell = app->values.v_tot / (float)app->config.cell_count;
        float soc =
            (avg_cell - app->config.v_cell_min) / (app->config.v_cell_max - app->config.v_cell_min);
        if (soc < 0.0f) {
            soc = 0.0f;
        } else if (soc > 1.0f) {
            soc = 1.0f;
        }
        app->values.soc = soc;
    }

    /* Safety checks */
    app->fault_code = 0;
    for (uint32_t i = 0; i < app->config.cell_count && i < BMS_MAX_CELLS; i++) {
        if (app->values.cell_voltages[i] > 0.1f) {
            if (app->values.cell_voltages[i] > app->config.v_cell_max + 0.1f) {
                app->fault_code |= (1u << 0); /* Over-voltage */
            }
            if (app->values.cell_voltages[i] < app->config.v_cell_min - 0.2f) {
                app->fault_code |= (1u << 1); /* Under-voltage */
            }
        }
    }

    for (uint32_t i = 0; i < BMS_MAX_TEMPS; i++) {
        if (app->values.temp_sensors[i] > app->config.temp_max_c && app->config.temp_max_c > 0.0f) {
            app->fault_code |= (1u << 2); /* Over-temperature */
        }
    }

    return EDGE_OK;
}

static edge_status_t vesc_bms_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t vesc_bms_power_off(edge_module_t *mod) {
    (void)mod;
    return EDGE_OK;
}

void vesc_bms_construct(vesc_bms_app_t *app, uint32_t module_id, uint32_t priority,
                        const bms_config_t *config, const bms_can_port_t *can_port) {
    if (!app) {
        return;
    }
    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 100u,
        .poll = vesc_bms_poll,
        .on_event = vesc_bms_on_event,
        .power_off = vesc_bms_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    } else {
        app->config = (bms_config_t){
            .cell_count = 12u,
            .v_cell_min = 3.0f,
            .v_cell_max = 4.2f,
            .temp_max_c = 65.0f,
            .i_in_max_a = 50.0f,
            .i_out_max_a = 80.0f,
            .soc_limit_start = 0.95f,
            .soc_limit_end = 1.0f,
        };
    }

    if (can_port) {
        app->can_port = *can_port;
    }
}

edge_status_t vesc_bms_init(vesc_bms_app_t *app) {
    if (!app) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t vesc_bms_process_can_frame(vesc_bms_app_t *app, uint32_t can_id, const uint8_t *data,
                                         uint8_t len) {
    if (!app || !data || len < 4) {
        return EDGE_EINVAL;
    }

    uint8_t cmd = (uint8_t)(can_id & 0xFF);
    if (cmd == 0x30) {
        /* V_TOT & Current: 2 bytes V_tot (0.1V), 2 bytes I_in (0.1A) */
        uint16_t v_raw = (uint16_t)((data[0] << 8) | data[1]);
        int16_t i_raw = (int16_t)((data[2] << 8) | data[3]);
        app->values.v_tot = (float)v_raw * 0.1f;
        app->values.i_in = (float)i_raw * 0.1f;
        return EDGE_OK;
    } else if (cmd == 0x31) {
        /* Cell voltages: 1 byte start_idx, following 2-byte cell mV */
        uint8_t start_idx = data[0];
        uint8_t num_cells = (uint8_t)((len - 1) / 2);
        for (uint8_t i = 0; i < num_cells; i++) {
            if (start_idx + i < BMS_MAX_CELLS) {
                uint16_t mv = (uint16_t)((data[1 + i * 2] << 8) | data[2 + i * 2]);
                app->values.cell_voltages[start_idx + i] = (float)mv * 0.001f;
            }
        }
        return EDGE_OK;
    } else if (cmd == 0x32) {
        /* Temps: 1 byte start_idx, following 1-byte temp C */
        uint8_t start_idx = data[0];
        for (uint8_t i = 1; i < len; i++) {
            if ((uint32_t)start_idx + (uint32_t)(i - 1) < BMS_MAX_TEMPS) {
                app->values.temp_sensors[start_idx + (i - 1)] = (float)(int8_t)data[i];
            }
        }
        return EDGE_OK;
    }

    return EDGE_OK;
}

void vesc_bms_update_limits(vesc_bms_app_t *app, float *i_in_min, float *i_in_max) {
    if (!app || !i_in_min || !i_in_max) {
        return;
    }

    float in_max = app->config.i_in_max_a;
    float out_max = app->config.i_out_max_a;

    if (app->fault_code != 0) {
        *i_in_min = 0.0f;
        *i_in_max = 0.0f;
        return;
    }

    /* Charge de-rating near 100% SOC */
    if (app->values.soc > app->config.soc_limit_start &&
        app->config.soc_limit_end > app->config.soc_limit_start) {
        float factor = 1.0f - (app->values.soc - app->config.soc_limit_start) /
                                  (app->config.soc_limit_end - app->config.soc_limit_start);
        if (factor < 0.0f) {
            factor = 0.0f;
        }
        in_max *= factor;
    }

    *i_in_min = -in_max;
    *i_in_max = out_max;
}

void vesc_bms_get_values(const vesc_bms_app_t *app, bms_values_t *out_val) {
    if (app && out_val) {
        *out_val = app->values;
    }
}

edge_module_t *vesc_bms_module(vesc_bms_app_t *app) {
    return app ? &app->module : NULL;
}
