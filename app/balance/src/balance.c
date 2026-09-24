#include "balance/balance.h"
#include <math.h>
#include <string.h>

static edge_status_t balance_poll(edge_module_t *mod) {
    balance_app_t *app = (balance_app_t *)edge_module_data(mod);
    return balance_update(app, 0.0f, 0.01f);
}

static edge_status_t balance_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t balance_power_off(edge_module_t *mod) {
    balance_app_t *app = (balance_app_t *)edge_module_data(mod);
    if (app) {
        app->current_demand_a = 0.0f;
        app->foot_engaged = false;
        app->integral = 0.0f;
    }
    return EDGE_OK;
}

void balance_construct(balance_app_t *app, uint32_t module_id, uint32_t priority,
                       const balance_config_t *config, const balance_port_t *port) {
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 10u, /* 100Hz balance loop */
        .poll = balance_poll,
        .on_event = balance_on_event,
        .power_off = balance_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (app->config.kp <= 0.0f) {
        app->config.kp = 1.5f;
    }
    if (app->config.fault_pitch_deg <= 0.0f) {
        app->config.fault_pitch_deg = 45.0f;
    }
    if (app->config.fault_roll_deg <= 0.0f) {
        app->config.fault_roll_deg = 45.0f;
    }
    if (app->config.max_current_a <= 0.0f) {
        app->config.max_current_a = 30.0f;
    }

    if (port) {
        app->port = *port;
    }
    app->foot_engaged = false;
    app->faulted = false;
}

edge_status_t balance_init(balance_app_t *app) {
    if (!app || !app->port.read_attitude) {
        return EDGE_EINVAL;
    }
    app->current_demand_a = 0.0f;
    app->integral = 0.0f;
    app->last_error = 0.0f;
    app->target_pitch_deg = 0.0f;
    app->foot_engaged = false;
    app->faulted = false;
    return EDGE_OK;
}

edge_status_t balance_update(balance_app_t *app, float motor_rpm, float dt) {
    if (!app || dt <= 0.0f) {
        return EDGE_EINVAL;
    }

    float pitch_deg = 0.0f, roll_deg = 0.0f, gyro_p_dps = 0.0f, gyro_r_dps = 0.0f;
    bool sw1 = false, sw2 = false;

    edge_status_t st = app->port.read_attitude(app->port.self, &pitch_deg, &roll_deg, &gyro_p_dps,
                                               &gyro_r_dps, &sw1, &sw2);
    if (st != EDGE_OK) {
        app->faulted = true;
        app->current_demand_a = 0.0f;
        return st;
    }

    /* Check footpad engagement */
    bool engaged = sw1 || sw2;
    app->foot_engaged = engaged;

    /* Safety checks for roll / pitch tilt threshold */
    if (fabsf(pitch_deg) > app->config.fault_pitch_deg ||
        fabsf(roll_deg) > app->config.fault_roll_deg) {
        app->faulted = true;
        app->current_demand_a = 0.0f;
        app->integral = 0.0f;
        return EDGE_OK;
    }

    if (!engaged) {
        app->current_demand_a = 0.0f;
        app->integral = 0.0f;
        app->faulted = false;
        return EDGE_OK;
    }

    app->faulted = false;

    /* Speed Tiltback calculation */
    float target_pitch = 0.0f;
    if (app->config.tiltback_speed_rpm > 0.0f &&
        fabsf(motor_rpm) > app->config.tiltback_speed_rpm) {
        float excess_speed = fabsf(motor_rpm) - app->config.tiltback_speed_rpm;
        target_pitch = (motor_rpm > 0.0f ? 1.0f : -1.0f) * (excess_speed * 0.005f);
        if (target_pitch > app->config.tiltback_angle_deg) {
            target_pitch = app->config.tiltback_angle_deg;
        }
        if (target_pitch < -app->config.tiltback_angle_deg) {
            target_pitch = -app->config.tiltback_angle_deg;
        }
    }
    app->target_pitch_deg = target_pitch;

    /* PID on pitch error */
    float error = (pitch_deg - target_pitch);
    if (fabsf(error) < app->config.deadband_deg) {
        error = 0.0f;
    }

    app->integral += error * dt;
    /* Anti-windup */
    if (app->integral > 20.0f)
        app->integral = 20.0f;
    if (app->integral < -20.0f)
        app->integral = -20.0f;

    float derivative = gyro_p_dps;

    float demand =
        (app->config.kp * error) + (app->config.ki * app->integral) + (app->config.kd * derivative);

    if (demand > app->config.max_current_a) {
        demand = app->config.max_current_a;
    }
    if (demand < -app->config.max_current_a) {
        demand = -app->config.max_current_a;
    }

    app->current_demand_a = demand;
    app->last_error = error;

    return EDGE_OK;
}

float balance_get_current_demand(const balance_app_t *app) {
    if (!app || app->faulted || !app->foot_engaged) {
        return 0.0f;
    }
    return app->current_demand_a;
}

bool balance_is_engaged(const balance_app_t *app) {
    if (!app) {
        return false;
    }
    return app->foot_engaged && !app->faulted;
}

edge_module_t *balance_module(balance_app_t *app) {
    return app ? &app->module : NULL;
}
