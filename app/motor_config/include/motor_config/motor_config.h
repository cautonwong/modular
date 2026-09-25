#ifndef APP_MOTOR_CONFIG_H
#define APP_MOTOR_CONFIG_H

#include "edge/errors.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOTOR_CONFIG_SIGNATURE 0x56455343u /* "VESC" */
/* 2: added the fields foc_core consumes (filter constant, PLL gains, max duty,
 * observer type). A stored blob from version 1 is rejected rather than parsed. */
#define MOTOR_CONFIG_SCHEMA_VER 2u
#define MOTOR_CONFIG_BUFFER_SIZE 256u

typedef enum {
    MC_MOTOR_TYPE_BLDC = 0,
    MC_MOTOR_TYPE_DC = 1,
    MC_MOTOR_TYPE_FOC = 2
} mc_motor_type_t;

typedef struct mc_configuration {
    mc_motor_type_t motor_type;
    float current_min;
    float current_max;
    float in_current_min;
    float in_current_max;
    float current_min_scale;
    float current_max_scale;
    float v_in_min;
    float v_in_max;
    float rpm_min;
    float rpm_max;
    float foc_current_kp;
    float foc_current_ki;
    float foc_f_sw;
    float foc_motor_r;
    float foc_motor_l;
    float foc_motor_flux_linkage;
    float foc_observer_gain;
    float temp_fet_max;
    float temp_motor_max;
    /* Speed information, reference types and defaults from datatypes.h:584 and
     * mcconf_default.h:613-620. si_motor_poles is a pole COUNT, not pole pairs. */
    uint8_t si_motor_poles;
    float si_gear_ratio;
    float si_wheel_diameter;
    /*
     * Fields foc_core actually consumes, with the reference's defaults, so the
     * configuration reaches the controller instead of the controller running on
     * compile-time constants. Defaults: mcconf_default.h (notes per field).
     */
    float foc_current_filter_const; /* 0.1   - MCCONF_FOC_CURRENT_FILTER_CONST */
    float foc_pll_kp;               /* 2000  - MCCONF_FOC_PLL_KP */
    float foc_pll_ki;               /* 30000 - MCCONF_FOC_PLL_KI */
    float l_max_duty;               /* 0.95  - MCCONF_L_MAX_DUTY */
    uint8_t foc_observer_type;      /* 0     - FOC_OBSERVER_ORTEGA_ORIGINAL */
} mc_configuration_t;

typedef struct app_configuration {
    uint8_t controller_id;
    uint32_t timeout_msec;
    float timeout_brake_current;
    uint32_t can_baud_rate;
} app_configuration_t;

/*
 * Consumer-Defined Storage Port (Rules: void *self; callbacks take void *self)
 */
typedef struct motor_config_storage_port {
    edge_status_t (*read)(void *self, uint32_t offset, uint8_t *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t offset, const uint8_t *buf, size_t len);
    edge_status_t (*erase)(void *self, uint32_t offset, size_t len);
    void *self;
} motor_config_storage_port_t;

typedef struct motor_config {
    edge_module_t module;

    /* Injected Storage Port */
    const motor_config_storage_port_t *storage;

    /* Active Configurations */
    mc_configuration_t mcconf;
    app_configuration_t appconf;

    /* Flash Offset */
    uint32_t flash_offset;
    bool is_dirty;
} motor_config_t;

void motor_config_set_defaults(mc_configuration_t *mcconf, app_configuration_t *appconf);

edge_status_t motor_config_validate(const mc_configuration_t *mcconf,
                                    const app_configuration_t *appconf);

edge_status_t motor_config_serialize(const mc_configuration_t *mcconf,
                                     const app_configuration_t *appconf, uint8_t *buffer,
                                     size_t buf_size, size_t *out_len);

edge_status_t motor_config_deserialize(mc_configuration_t *mcconf, app_configuration_t *appconf,
                                       const uint8_t *buffer, size_t len);

void motor_config_construct(motor_config_t *self, uint32_t module_id, uint32_t priority,
                            const motor_config_storage_port_t *storage, uint32_t flash_offset);

edge_status_t motor_config_init(motor_config_t *self);
edge_status_t motor_config_deinit(motor_config_t *self);
edge_status_t motor_config_load(motor_config_t *self);
edge_status_t motor_config_save(motor_config_t *self);

edge_module_t *motor_config_module(motor_config_t *self);
const mc_configuration_t *motor_config_get_mc(const motor_config_t *self);
const app_configuration_t *motor_config_get_app(const motor_config_t *self);
edge_status_t motor_config_update_mc(motor_config_t *self, const mc_configuration_t *mcconf);
edge_status_t motor_config_update_app(motor_config_t *self, const app_configuration_t *appconf);

#ifdef __cplusplus
}
#endif

#endif /* APP_MOTOR_CONFIG_H */
