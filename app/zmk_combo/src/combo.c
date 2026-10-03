#include "zmk_combo/combo.h"

static edge_status_t zmk_combo_poll(edge_module_t *module) {
    const zmk_combo_app_t *self = (const zmk_combo_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_combo_power_off(edge_module_t *module) {
    zmk_combo_app_t *self = (zmk_combo_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < ZMK_COMBO_MAX_COMBOS; ++i)
        self->states[i] = (__typeof__(self->states[0])){0};
    for (size_t i = 0; i < sizeof(self->key_pressed); ++i)
        self->key_pressed[i] = false;
    return EDGE_OK;
}

void zmk_combo_construct(zmk_combo_app_t *self, uint32_t module_id, uint32_t priority,
                         const zmk_combo_behavior_if_t *behavior_port) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module = (edge_module_t){
        .module_id = module_id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .next_due = 0u,
        .poll = zmk_combo_poll,
        .on_event = NULL,
        .power_off = zmk_combo_power_off,
        .private_data = self,
    };
    self->behavior_port = behavior_port;
}

edge_status_t zmk_combo_init(zmk_combo_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < ZMK_COMBO_MAX_COMBOS; ++i)
        self->states[i] = (__typeof__(self->states[0])){0};
    for (size_t i = 0; i < sizeof(self->key_pressed); ++i)
        self->key_pressed[i] = false;
    return EDGE_OK;
}

edge_status_t zmk_combo_shutdown(zmk_combo_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < ZMK_COMBO_MAX_COMBOS; ++i)
        self->states[i] = (__typeof__(self->states[0])){0};
    return EDGE_OK;
}

edge_status_t zmk_combo_add_combo(zmk_combo_app_t *self, const zmk_combo_config_t *config) {
    if (self == NULL || config == NULL || self->combo_count >= ZMK_COMBO_MAX_COMBOS ||
        config->position_count == 0 || config->position_count > ZMK_COMBO_MAX_KEYS_PER_COMBO) {
        return EDGE_EINVAL;
    }
    self->configs[self->combo_count] = *config;
    if (self->configs[self->combo_count].timeout_ms == 0) {
        self->configs[self->combo_count].timeout_ms = 50; /* Default 50ms */
    }
    self->combo_count++;
    return EDGE_OK;
}

bool zmk_combo_process_key(zmk_combo_app_t *self, uint32_t position, bool pressed,
                           uint32_t timestamp_ms, uint32_t active_layers_mask) {
    if (self == NULL || position >= ZMK_COMBO_MAX_POSITIONS) {
        return false;
    }

    if (pressed) {
        bool handled = false;
        self->key_pressed[position] = true;
        self->key_press_time[position] = timestamp_ms;

        for (uint8_t c = 0; c < self->combo_count; c++) {
            const zmk_combo_config_t *cfg = &self->configs[c];
            zmk_combo_state_t *st = &self->states[c];

            if (cfg->layers_mask != 0 && (active_layers_mask & cfg->layers_mask) == 0) {
                continue;
            }

            for (uint8_t k = 0; k < cfg->position_count; k++) {
                if (cfg->positions[k] == position) {
                    if (st->pressed_mask == 0) {
                        /* Check prior idle requirement */
                        if (cfg->require_prior_idle_ms > 0 && self->last_activity_time_ms > 0) {
                            if ((timestamp_ms - self->last_activity_time_ms) <
                                cfg->require_prior_idle_ms) {
                                break; /* Skipped due to idle requirement */
                            }
                        }
                        st->start_time_ms = timestamp_ms;
                    }
                    st->pressed_mask |= (uint8_t)(1u << k);

                    uint8_t full_mask = (uint8_t)((1u << cfg->position_count) - 1u);
                    if (st->pressed_mask == full_mask) {
                        /* Check timeout */
                        if ((timestamp_ms - st->start_time_ms) <= cfg->timeout_ms) {
                            st->active = true;
                            if (self->behavior_port != NULL &&
                                self->behavior_port->invoke_binding != NULL) {
                                self->behavior_port->invoke_binding(
                                    self->behavior_port->self, cfg->binding.behavior_id,
                                    cfg->binding.param1, cfg->binding.param2, true, timestamp_ms);
                            }
                            handled = true;
                        }
                    }
                }
            }
        }
        self->last_activity_time_ms = timestamp_ms;
        return handled;
    } else {
        bool handled = false;
        self->key_pressed[position] = false;

        for (uint8_t c = 0; c < self->combo_count; c++) {
            const zmk_combo_config_t *cfg = &self->configs[c];
            zmk_combo_state_t *st = &self->states[c];

            for (uint8_t k = 0; k < cfg->position_count; k++) {
                if (cfg->positions[k] == position) {
                    st->pressed_mask &= (uint8_t) ~(1u << k);
                    if (st->active) {
                        if (cfg->slow_release && st->pressed_mask != 0) {
                            /* Slow release: don't release until all constituent keys are released
                             */
                            handled = true;
                            continue;
                        }
                        st->active = false;
                        if (self->behavior_port != NULL &&
                            self->behavior_port->invoke_binding != NULL) {
                            self->behavior_port->invoke_binding(
                                self->behavior_port->self, cfg->binding.behavior_id,
                                cfg->binding.param1, cfg->binding.param2, false, timestamp_ms);
                        }
                        handled = true;
                    }
                }
            }
        }
        self->last_activity_time_ms = timestamp_ms;
        return handled;
    }
}
