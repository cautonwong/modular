#include "zmk_keymap/keymap.h"

static void check_conditional_layers(zmk_keymap_app_t *self) {
    if (self == NULL) {
        return;
    }
    for (uint8_t i = 0; i < self->conditional_layer_count; i++) {
        const zmk_conditional_layer_t *cond = &self->conditional_layers[i];
        if (cond->target_layer >= ZMK_KEYMAP_MAX_LAYERS) {
            continue;
        }
        /* Check if trigger mask is fully satisfied */
        if ((self->active_layers_mask & cond->trigger_mask) == cond->trigger_mask) {
            self->active_layers_mask |= (1u << cond->target_layer);
        } else {
            self->active_layers_mask &= ~(1u << cond->target_layer);
        }
    }
}

static edge_status_t zmk_keymap_poll(edge_module_t *module) {
    const zmk_keymap_app_t *self = (const zmk_keymap_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_keymap_power_off(edge_module_t *module) {
    zmk_keymap_app_t *self = (zmk_keymap_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->position_active); ++i)
        self->position_active[i] = false;
    return EDGE_OK;
}

void zmk_keymap_construct(zmk_keymap_app_t *self, uint32_t module_id, uint32_t priority,
                          const zmk_keymap_behavior_if_t *behavior_port,
                          const zmk_keymap_event_sink_if_t *sink, uint8_t layer_count,
                          uint8_t position_count) {
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
        .poll = zmk_keymap_poll,
        .on_event = NULL,
        .power_off = zmk_keymap_power_off,
        .private_data = self,
    };
    self->behavior_port = behavior_port;
    self->sink = sink;
    self->layer_count = (layer_count > ZMK_KEYMAP_MAX_LAYERS) ? ZMK_KEYMAP_MAX_LAYERS : layer_count;
    self->position_count =
        (position_count > ZMK_KEYMAP_MAX_POSITIONS) ? ZMK_KEYMAP_MAX_POSITIONS : position_count;
    self->default_layer = 0;
    self->active_layers_mask = 1u; /* Layer 0 active by default */
}

edge_status_t zmk_keymap_init(zmk_keymap_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->active_layers_mask = (1u << self->default_layer);
    for (size_t i = 0; i < sizeof(self->position_active) / sizeof(self->position_active[0]); ++i)
        self->position_active[i] = false;
    for (size_t i = 0; i < sizeof(self->pressed_layer) / sizeof(self->pressed_layer[0]); ++i)
        self->pressed_layer[i] = 0;
    for (size_t i = 0; i < sizeof(self->pressed_binding) / sizeof(self->pressed_binding[0]); ++i)
        self->pressed_binding[i] = (zmk_behavior_binding_t){0};
    return EDGE_OK;
}

edge_status_t zmk_keymap_shutdown(zmk_keymap_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->position_active); ++i)
        self->position_active[i] = false;
    return EDGE_OK;
}

edge_status_t zmk_keymap_set_binding(zmk_keymap_app_t *self, uint8_t layer, uint8_t position,
                                     zmk_behavior_binding_t binding) {
    if (self == NULL || layer >= ZMK_KEYMAP_MAX_LAYERS || position >= ZMK_KEYMAP_MAX_POSITIONS) {
        return EDGE_EINVAL;
    }
    self->bindings[layer][position] = binding;
    return EDGE_OK;
}

edge_status_t zmk_keymap_get_binding(const zmk_keymap_app_t *self, uint8_t layer, uint8_t position,
                                     zmk_behavior_binding_t *out_binding) {
    if (self == NULL || out_binding == NULL || layer >= ZMK_KEYMAP_MAX_LAYERS ||
        position >= ZMK_KEYMAP_MAX_POSITIONS) {
        return EDGE_EINVAL;
    }
    *out_binding = self->bindings[layer][position];
    return EDGE_OK;
}

edge_status_t zmk_keymap_layer_activate(zmk_keymap_app_t *self, uint8_t layer) {
    if (self == NULL || layer >= ZMK_KEYMAP_MAX_LAYERS) {
        return EDGE_EINVAL;
    }
    self->active_layers_mask |= (1u << layer);
    check_conditional_layers(self);
    if (self->sink != NULL && self->sink->post_layer_event != NULL) {
        self->sink->post_layer_event(self->sink->self, layer, true);
    }
    return EDGE_OK;
}

edge_status_t zmk_keymap_layer_deactivate(zmk_keymap_app_t *self, uint8_t layer) {
    if (self == NULL || layer >= ZMK_KEYMAP_MAX_LAYERS) {
        return EDGE_EINVAL;
    }
    self->active_layers_mask &= ~(1u << layer);
    /* Always keep default layer active */
    self->active_layers_mask |= (1u << self->default_layer);
    check_conditional_layers(self);
    if (self->sink != NULL && self->sink->post_layer_event != NULL) {
        self->sink->post_layer_event(self->sink->self, layer, false);
    }
    return EDGE_OK;
}

edge_status_t zmk_keymap_layer_toggle(zmk_keymap_app_t *self, uint8_t layer) {
    if (self == NULL || layer >= ZMK_KEYMAP_MAX_LAYERS) {
        return EDGE_EINVAL;
    }
    if (zmk_keymap_layer_is_active(self, layer)) {
        return zmk_keymap_layer_deactivate(self, layer);
    } else {
        return zmk_keymap_layer_activate(self, layer);
    }
}

edge_status_t zmk_keymap_layer_to(zmk_keymap_app_t *self, uint8_t layer) {
    if (self == NULL || layer >= ZMK_KEYMAP_MAX_LAYERS) {
        return EDGE_EINVAL;
    }
    self->default_layer = layer;
    self->active_layers_mask = (1u << layer);
    check_conditional_layers(self);
    if (self->sink != NULL && self->sink->post_layer_event != NULL) {
        self->sink->post_layer_event(self->sink->self, layer, true);
    }
    return EDGE_OK;
}

bool zmk_keymap_layer_is_active(const zmk_keymap_app_t *self, uint8_t layer) {
    if (self == NULL || layer >= ZMK_KEYMAP_MAX_LAYERS) {
        return false;
    }
    return (self->active_layers_mask & (1u << layer)) != 0u;
}

uint32_t zmk_keymap_get_active_layers_mask(const zmk_keymap_app_t *self) {
    if (self == NULL) {
        return 0;
    }
    return self->active_layers_mask;
}

edge_status_t zmk_keymap_add_conditional_layer(zmk_keymap_app_t *self, uint32_t trigger_mask,
                                               uint8_t target_layer) {
    if (self == NULL || target_layer >= ZMK_KEYMAP_MAX_LAYERS ||
        self->conditional_layer_count >= ZMK_KEYMAP_MAX_CONDITIONAL_LAYERS) {
        return EDGE_EINVAL;
    }
    self->conditional_layers[self->conditional_layer_count++] = (zmk_conditional_layer_t){
        .trigger_mask = trigger_mask,
        .target_layer = target_layer,
    };
    check_conditional_layers(self);
    return EDGE_OK;
}

edge_status_t zmk_keymap_on_position_state_change(zmk_keymap_app_t *self, uint32_t position,
                                                  bool pressed, uint32_t timestamp_ms) {
    if (self == NULL || position >= ZMK_KEYMAP_MAX_POSITIONS) {
        return EDGE_EINVAL;
    }

    if (pressed) {
        /* Find highest active layer with non-transparent binding */
        zmk_behavior_binding_t binding = {0};
        uint8_t resolved_layer = 0;
        bool found = false;

        for (int l = (int)ZMK_KEYMAP_MAX_LAYERS - 1; l >= 0; l--) {
            if ((self->active_layers_mask & (1u << l)) != 0u) {
                zmk_behavior_binding_t candidate = self->bindings[l][position];
                if (candidate.behavior_id != ZMK_BHV_NONE &&
                    candidate.behavior_id != ZMK_BHV_TRANS) {
                    binding = candidate;
                    resolved_layer = (uint8_t)l;
                    found = true;
                    break;
                }
            }
        }

        if (!found) {
            /* Fallback to default layer binding */
            binding = self->bindings[self->default_layer][position];
            resolved_layer = self->default_layer;
        }

        self->position_active[position] = true;
        self->pressed_layer[position] = resolved_layer;
        self->pressed_binding[position] = binding;

        /* Handle built-in layer behaviors directly if not intercepted */
        if (binding.behavior_id == ZMK_BHV_MO) {
            zmk_keymap_layer_activate(self, (uint8_t)binding.param1);
        } else if (binding.behavior_id == ZMK_BHV_TOG) {
            zmk_keymap_layer_toggle(self, (uint8_t)binding.param1);
        } else if (binding.behavior_id == ZMK_BHV_TO) {
            zmk_keymap_layer_to(self, (uint8_t)binding.param1);
        }

        if (self->behavior_port != NULL && self->behavior_port->invoke_binding != NULL) {
            return self->behavior_port->invoke_binding(self->behavior_port->self,
                                                       binding.behavior_id, binding.param1,
                                                       binding.param2, true, timestamp_ms);
        }
        return EDGE_OK;
    } else {
        /* Key release */
        if (!self->position_active[position]) {
            return EDGE_OK;
        }
        self->position_active[position] = false;
        zmk_behavior_binding_t binding = self->pressed_binding[position];

        if (binding.behavior_id == ZMK_BHV_MO) {
            zmk_keymap_layer_deactivate(self, (uint8_t)binding.param1);
        }

        if (self->behavior_port != NULL && self->behavior_port->invoke_binding != NULL) {
            return self->behavior_port->invoke_binding(self->behavior_port->self,
                                                       binding.behavior_id, binding.param1,
                                                       binding.param2, false, timestamp_ms);
        }
        return EDGE_OK;
    }
}
