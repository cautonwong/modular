#include "motor_id/motor_id.h"

#include <math.h>
#include <string.h>

#define M_PI_F 3.14159265358979323846f

static edge_status_t motor_id_poll(edge_module_t *mod) {
    (void)mod;
    return EDGE_OK;
}

static edge_status_t motor_id_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t motor_id_power_off(edge_module_t *mod) {
    motor_id_app_t *app = (motor_id_app_t *)edge_module_data(mod);
    if (app && app->control_port.stop_inverter) {
        app->control_port.stop_inverter(app->control_port.self);
    }
    if (app) {
        app->state = MOTOR_ID_STATE_IDLE;
    }
    return EDGE_OK;
}

void motor_id_construct(motor_id_app_t *app, uint32_t module_id, uint32_t priority,
                        const motor_id_config_t *config,
                        const motor_id_measure_port_t *measure_port,
                        const motor_id_control_port_t *control_port) {
    if (!app) {
        return;
    }

    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .poll = motor_id_poll,
        .on_event = motor_id_on_event,
        .power_off = motor_id_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (app->config.samples_r == 0) {
        app->config.samples_r = 100;
    }
    if (app->config.samples_l == 0) {
        app->config.samples_l = 100;
    }
    if (app->config.max_current <= 0.0f) {
        app->config.max_current = 10.0f;
    }

    if (measure_port) {
        app->measure_port = *measure_port;
    }
    if (control_port) {
        app->control_port = *control_port;
    }
    app->state = MOTOR_ID_STATE_IDLE;
}

edge_status_t motor_id_init(motor_id_app_t *app) {
    if (!app || !app->measure_port.get_currents || !app->control_port.set_voltage_alpha_beta) {
        return EDGE_EINVAL;
    }
    app->state = MOTOR_ID_STATE_IDLE;
    return EDGE_OK;
}

edge_status_t motor_id_start_r_l(motor_id_app_t *app) {
    if (!app) {
        return EDGE_EINVAL;
    }
    app->state = MOTOR_ID_STATE_MEASURING_R;
    app->step_count = 0;
    app->accum_v = 0.0f;
    app->accum_i = 0.0f;
    app->current_applied_v = 0.1f;
    app->result.valid = false;
    return EDGE_OK;
}

edge_status_t motor_id_start_flux(motor_id_app_t *app) {
    if (!app) {
        return EDGE_EINVAL;
    }
    app->state = MOTOR_ID_STATE_SPIN_FLUX;
    app->step_count = 0;
    app->openloop_angle = 0.0f;
    return EDGE_OK;
}

edge_status_t motor_id_start_hall(motor_id_app_t *app) {
    if (!app) {
        return EDGE_EINVAL;
    }
    app->state = MOTOR_ID_STATE_DETECT_HALL;
    app->step_count = 0;
    app->openloop_angle = 0.0f;
    for (int i = 0; i < 8; i++) {
        app->result.hall_table[i] = 255;
    }
    return EDGE_OK;
}

edge_status_t motor_id_step(motor_id_app_t *app, float dt) {
    if (!app || dt <= 0.0f) {
        return EDGE_EINVAL;
    }

    float i_alpha = 0.0f, i_beta = 0.0f;
    edge_status_t st = app->measure_port.get_currents(app->measure_port.self, &i_alpha, &i_beta);
    if (st != EDGE_OK) {
        app->state = MOTOR_ID_STATE_FAILED;
        return st;
    }

    switch (app->state) {
    case MOTOR_ID_STATE_IDLE:
    case MOTOR_ID_STATE_COMPLETE:
    case MOTOR_ID_STATE_FAILED:
        return EDGE_OK;

    case MOTOR_ID_STATE_MEASURING_R: {
        app->control_port.set_voltage_alpha_beta(app->control_port.self, app->current_applied_v,
                                                 0.0f);

        if (fabsf(i_alpha) < app->config.max_current && app->current_applied_v < 24.0f) {
            app->current_applied_v += 0.05f;
        }

        if (fabsf(i_alpha) > 0.1f) {
            app->accum_v += app->current_applied_v;
            app->accum_i += fabsf(i_alpha);
            app->step_count++;
        }

        if (app->step_count >= app->config.samples_r) {
            if (app->accum_i > 0.001f) {
                app->result.r_ohm = (app->accum_v / app->accum_i) * (2.0f / 3.0f);
            }
            app->state = MOTOR_ID_STATE_MEASURING_L;
            app->step_count = 0;
            app->accum_v = 0.0f;
            app->accum_i = 0.0f;
            app->current_applied_v = 1.0f;
        }
        break;
    }

    case MOTOR_ID_STATE_MEASURING_L: {
        float v_test =
            (app->step_count % 2 == 0) ? app->current_applied_v : -app->current_applied_v;
        app->control_port.set_voltage_alpha_beta(app->control_port.self, v_test, 0.0f);

        float di = fabsf(i_alpha);
        if (di > 0.01f) {
            app->accum_i += di;
            app->step_count++;
        } else {
            app->step_count++;
        }

        if (app->step_count >= app->config.samples_l) {
            float avg_di = app->accum_i / (float)app->config.samples_l;
            if (avg_di > 0.0001f) {
                app->result.l_henry = (app->current_applied_v * dt) / avg_di;
            } else {
                app->result.l_henry = 0.0001f;
            }
            app->control_port.stop_inverter(app->control_port.self);
            app->state = MOTOR_ID_STATE_COMPLETE;
            app->result.valid = true;
        }
        break;
    }

    case MOTOR_ID_STATE_SPIN_FLUX: {
        float speed_rad_s = (app->config.target_erpm > 0.0f ? app->config.target_erpm : 1000.0f) *
                            (2.0f * M_PI_F / 60.0f);
        app->openloop_angle += speed_rad_s * dt;
        while (app->openloop_angle > 2.0f * M_PI_F) {
            app->openloop_angle -= 2.0f * M_PI_F;
        }

        if (app->control_port.set_openloop_angle) {
            app->control_port.set_openloop_angle(app->control_port.self, app->openloop_angle,
                                                 app->config.max_current * 0.5f);
        }

        app->step_count++;
        if (app->step_count >= 200) {
            float v_bus = 24.0f;
            if (app->measure_port.get_vbus) {
                app->measure_port.get_vbus(app->measure_port.self, &v_bus);
            }
            float v_q = v_bus * 0.5f;
            float i_q = fabsf(i_beta);
            float r = (app->result.r_ohm > 0.001f) ? app->result.r_ohm : 0.05f;
            if (speed_rad_s > 1.0f) {
                app->result.flux_linkage_wb = (v_q - i_q * r) / speed_rad_s;
            } else {
                app->result.flux_linkage_wb = 0.005f;
            }
            app->control_port.stop_inverter(app->control_port.self);
            app->state = MOTOR_ID_STATE_COMPLETE;
            app->result.valid = true;
        }
        break;
    }

    case MOTOR_ID_STATE_DETECT_HALL: {
        float step_angle = (2.0f * M_PI_F) / 60.0f;
        app->openloop_angle += step_angle;
        if (app->control_port.set_openloop_angle) {
            app->control_port.set_openloop_angle(app->control_port.self, app->openloop_angle,
                                                 app->config.max_current * 0.5f);
        }

        if (app->measure_port.get_hall) {
            uint8_t hall = app->measure_port.get_hall(app->measure_port.self) & 0x07;
            if (hall > 0 && hall < 7) {
                uint8_t sector = (uint8_t)((app->openloop_angle / (2.0f * M_PI_F)) * 6.0f) % 6;
                app->result.hall_table[hall] = sector;
            }
        }

        app->step_count++;
        if (app->step_count >= 60) {
            app->control_port.stop_inverter(app->control_port.self);
            app->state = MOTOR_ID_STATE_COMPLETE;
            app->result.valid = true;
        }
        break;
    }
    }

    return EDGE_OK;
}

const motor_id_result_t *motor_id_get_result(const motor_id_app_t *app) {
    if (!app) {
        return NULL;
    }
    return &app->result;
}

edge_module_t *motor_id_module(motor_id_app_t *app) {
    return app ? &app->module : NULL;
}
