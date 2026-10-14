#include "pas/pas.h"

#include <math.h>
#include <string.h>

/*
 * applications/app_pas.c:34-40's constants.
 */
#define PAS_PEDAL_INPUT_TIMEOUT 0.2f
#define PAS_MAX_MS_WITHOUT_CADENCE_OR_TORQUE 5000.0f
#define PAS_MAX_MS_WITHOUT_CADENCE 1000.0f
#define PAS_MIN_MS_WITHOUT_POWER 500.0f

static edge_status_t pas_poll(edge_module_t *mod) {
    pas_app_t *app = (pas_app_t *)edge_module_data(mod);
    /* The reference's own period: one tick of update_rate_hz, at least one tick long
     * (app_pas.c:193). */
    float dt = 1.0f / ((float)app->config.update_rate_hz);
    if (dt <= 0.0f) {
        dt = 1.0f;
    }
    return pas_update(app, dt);
}

static edge_status_t pas_on_event(edge_module_t *mod, const edge_event_t *evt) {
    (void)mod;
    (void)evt;
    return EDGE_OK;
}

static edge_status_t pas_power_off(edge_module_t *mod) {
    pas_app_t *app = (pas_app_t *)edge_module_data(mod);
    if (app) {
        app->output_current_rel = 0.0f;
        app->active = false;
    }
    return EDGE_OK;
}

void pas_construct(pas_app_t *app, uint32_t module_id, uint32_t priority,
                   const pas_config_t *config, const pas_port_t *port) {
    if (!app) {
        return;
    }
    memset(app, 0, sizeof(*app));
    app->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 20u,
        .poll = pas_poll,
        .on_event = pas_on_event,
        .power_off = pas_power_off,
        .private_data = app,
    };

    if (config) {
        app->config = *config;
    }
    if (port) {
        app->port = *port;
    }
    app->sub_scaling = 1.0f;
    /* app_pas.c:62-67, app_pas_configure's own arithmetic. */
    if (app->config.update_rate_hz == 0u) {
        app->config.update_rate_hz = 50u;
    }
    app->direction_conf = app->config.invert_pedal_direction ? -1.0f : 1.0f;
    app->max_pulse_period =
        1.0f / ((app->config.pedal_rpm_start / 60.0f) * (float)app->config.magnets) * 1.2f;
    app->min_pedal_period = 1.0f / (app->config.pedal_rpm_end * 3.0f / 60.0f);
    /* app_pas.c:64: ms_without_power starts at zero rather than the negative the reference's reset
     * world would give it, and output_current_rel starts clear. */
    app->ms_without_power = 0.0f;
    app->output_current_rel = 0.0f;
}

edge_status_t pas_init(pas_app_t *app) {
    if (!app || !app->port.read_levels || !app->port.now_seconds) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

/*
 * applications/app_pas.c:126-177, the whole of pas_event_handler. It is called once a period by the
 * thread rather than from an interrupt, which the reference says is possible (app_pas.c:187).
 */
void pas_event_handler(pas_app_t *app) {
    if (!app || !app->port.read_levels || !app->port.now_seconds) {
        return;
    }

    uint8_t pas1 = 0u;
    uint8_t pas2 = 0u;
    if (app->port.read_levels(app->port.self, &pas1, &pas2) != EDGE_OK) {
        return;
    }

    /* app_pas.c:128: the quadrature encoder matrix, in the reference's own order. */
    static const int8_t QEM[] = {0, -1, 1, 2, 1, 0, 2, -1, -1, 2, 0, 1, 2, 1, -1, 0};

    const uint8_t new_state = (uint8_t)(pas2 * 2u + pas1);
    const int8_t direction_qem = QEM[app->old_state * 4u + new_state];
    app->old_state = new_state;

    /* Several quadrature events in the right direction are required, so that vibrations cannot
     * engage the application on their own. */
    const int8_t direction = (int8_t)(app->direction_conf * (float)direction_qem);
    if (direction == 1) {
        app->correct_direction_counter++;
    } else if (direction == -1) {
        app->correct_direction_counter = 0;
    }

    float timestamp = 0.0f;
    app->port.now_seconds(app->port.self, &timestamp);

    /* The sensors are poorly placed, so only one rising edge is used as a reference. */
    if (new_state == 3u && app->correct_direction_counter >= 4) {
        float period = (timestamp - app->old_timestamp) * (float)app->config.magnets;
        app->old_timestamp = timestamp;

        /* UTILS_LP_FAST(period_filtered, period, 1.0) - the reference's own filter constant, one,
         * which passes the period through unchanged. It is written as the filter it is. */
        app->period_filtered -= 1.0f * (app->period_filtered - period);

        if (app->period_filtered < app->min_pedal_period) {
            return; /* Too short to be real, and the reference returns before touching the rest. */
        }

        app->pedal_rpm = 60.0f / app->period_filtered;
        app->pedal_rpm *= (app->direction_conf * (float)direction_qem);
        app->inactivity_time = 0.0f;
        app->correct_direction_counter = 0;
    } else {
        app->inactivity_time += 1.0f / (float)app->config.update_rate_hz;
        if (app->inactivity_time > app->max_pulse_period) {
            app->pedal_rpm = 0.0f;
        }
    }
}

/*
 * applications/app_pas.c:178-317, the thread body's work. What the reference does with mc_interface
 * and timeout calls is left to the product: this decides, and the product commands.
 */
edge_status_t pas_update(pas_app_t *app, float dt) {
    if (!app || dt < 0.0f) {
        return EDGE_EINVAL;
    }

    pas_event_handler(app);

    /* A fault is not this application's to observe; the product tells it when one has been raised,
     * and this is where the reference restarts the safe start's clock for it. */
    if (app->fault) {
        app->ms_without_power = 0.0f;
    }

    const float period_ms = 1000.0f / (float)app->config.update_rate_hz;
    float output = 0.0f;

    switch (app->config.ctrl_type) {
    case PAS_MODE_NONE:
        output = 0.0f;
        break;

    case PAS_MODE_CADENCE:
        /* With the same limits at both ends a numerical instability is approached, so that case is
         * on and off instead - which is what setting both limits the same means. */
        if (app->config.pedal_rpm_end > (app->config.pedal_rpm_start + 1.0f)) {
            output = (app->pedal_rpm - app->config.pedal_rpm_start) /
                     (app->config.pedal_rpm_end - app->config.pedal_rpm_start) *
                     (app->config.current_scaling * app->sub_scaling);
            if (output > app->config.current_scaling * app->sub_scaling) {
                output = app->config.current_scaling * app->sub_scaling;
            }
            if (output < 0.0f) {
                output = 0.0f;
            }
        } else {
            output = (app->pedal_rpm > app->config.pedal_rpm_end)
                         ? (app->config.current_scaling * app->sub_scaling)
                         : 0.0f;
        }
        break;

    case PAS_MODE_TORQUE:
    case PAS_MODE_TORQUE_WITH_CADENCE_TIMEOUT: {
        bool torque_sensed = false;
        if (app->config.ctrl_type == PAS_MODE_TORQUE && app->port.read_torque_ratio) {
            float ratio = 0.0f;
            if (app->port.read_torque_ratio(app->port.self, &ratio) == EDGE_OK) {
                app->torque_ratio = ratio;
                output = ratio * app->config.current_scaling * app->sub_scaling;
                if (output > app->config.current_scaling * app->sub_scaling) {
                    output = app->config.current_scaling * app->sub_scaling;
                }
                if (output < 0.0f) {
                    output = 0.0f;
                }
                torque_sensed = true;
            }
        }

        /* Assistance stops if torque has been sensed for five seconds without any pedal movement,
         * which is what keeps a rider resting on the pedals from heating the motor. */
        if (!torque_sensed || output == 0.0f || app->pedal_rpm > 0.0f) {
            app->ms_without_cadence_or_torque = 0.0f;
        } else {
            app->ms_without_cadence_or_torque += (1000.0f * period_ms) / 1000.0f;
            if (app->ms_without_cadence_or_torque > PAS_MAX_MS_WITHOUT_CADENCE_OR_TORQUE) {
                output = 0.0f;
            }
        }

        /* Still cranks mean still no output, which covers a torque sensor stuck at a non-zero
         * value. */
        if (app->pedal_rpm < 0.01f) {
            app->ms_without_cadence += (1000.0f * period_ms) / 1000.0f;
            if (app->ms_without_cadence > PAS_MAX_MS_WITHOUT_CADENCE) {
                output = 0.0f;
            }
        } else {
            app->ms_without_cadence = 0.0f;
        }
        break;
    }

    default:
        break;
    }

    /* The ramp, in the reference's own two directions, with the step being the elapsed time over
     * the ramp's own time - the port's dt is that elapsed time. */
    const float ramp_time = (fabsf(output) > fabsf(app->output_ramp)) ? app->config.ramp_time_pos
                                                                      : app->config.ramp_time_neg;
    if (ramp_time > 0.01f) {
        const float ramp_step = dt / ramp_time;
        if (app->output_ramp < output) {
            app->output_ramp =
                (app->output_ramp + ramp_step < output) ? (app->output_ramp + ramp_step) : output;
        } else if (app->output_ramp > output) {
            app->output_ramp =
                (app->output_ramp - ramp_step > output) ? (app->output_ramp - ramp_step) : output;
        }
        if (app->output_ramp < 0.0f) {
            app->output_ramp = 0.0f;
        }
        if (app->output_ramp > app->config.current_scaling * app->sub_scaling) {
            app->output_ramp = app->config.current_scaling * app->sub_scaling;
        }
        output = app->output_ramp;
    }

    if (output < 0.001f) {
        app->ms_without_power += (1000.0f * period_ms) / 1000.0f;
    }

    /* The safe start: enabled while the output has not been at zero for long enough, and, since the
     * count is compared with itself, only let go when it has held still twice. */
    if (app->ms_without_power < PAS_MIN_MS_WITHOUT_POWER) {
        if (app->ms_without_power == (float)app->pulses_without_power_before) {
            app->ms_without_power = 0.0f;
        }
        app->pulses_without_power_before = (int)app->ms_without_power;
        app->output_current_rel = 0.0f;
        app->active = true;
        return EDGE_OK;
    }

    app->output_current_rel = output;
    app->active = true;
    return EDGE_OK;
}

float pas_get_current_target_rel(const pas_app_t *app) {
    return app ? app->output_current_rel : 0.0f;
}

float pas_get_pedal_rpm(const pas_app_t *app) {
    return app ? app->pedal_rpm : 0.0f;
}

void pas_set_current_sub_scaling(pas_app_t *app, float current_sub_scaling) {
    if (app) {
        app->sub_scaling = current_sub_scaling;
    }
}

void pas_set_primary_output(pas_app_t *app, bool primary_output) {
    if (app) {
        app->primary_output = primary_output;
    }
}

void pas_set_fault(pas_app_t *app, bool fault) {
    if (app) {
        app->fault = fault;
    }
}

bool pas_is_primary_output(const pas_app_t *app) {
    return app ? app->primary_output : false;
}

bool pas_is_active(const pas_app_t *app) {
    return app ? app->active : false;
}

edge_module_t *pas_module(pas_app_t *app) {
    return app ? &app->module : NULL;
}
