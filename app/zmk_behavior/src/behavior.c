#include "zmk_behavior/behavior.h"

static edge_status_t zmk_behavior_poll(edge_module_t *module) {
    zmk_behavior_app_t *self = (zmk_behavior_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return zmk_behavior_tick(self, self->current_time_ms);
}

static edge_status_t zmk_behavior_power_off(edge_module_t *module) {
    zmk_behavior_app_t *self = (zmk_behavior_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->sticky_key = (__typeof__(self->sticky_key)){0};
    self->caps_word = (__typeof__(self->caps_word)){0};
    for (size_t i = 0; i < sizeof(self->key_toggle_states); ++i)
        self->key_toggle_states[i] = 0;
    self->active_modifiers = 0;
    return EDGE_OK;
}

void zmk_behavior_construct(zmk_behavior_app_t *self, uint32_t module_id, uint32_t priority,
                            const zmk_behavior_hid_if_t *hid,
                            const zmk_behavior_keymap_if_t *keymap) {
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
        .poll = zmk_behavior_poll,
        .on_event = NULL,
        .power_off = zmk_behavior_power_off,
        .private_data = self,
    };
    self->hid = hid;
    self->keymap = keymap;
}

edge_status_t zmk_behavior_init(zmk_behavior_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->sticky_key = (__typeof__(self->sticky_key)){0};
    self->caps_word = (__typeof__(self->caps_word)){0};
    for (size_t i = 0; i < sizeof(self->key_toggle_states); ++i)
        self->key_toggle_states[i] = 0;
    self->active_modifiers = 0;
    self->last_keycode = 0;
    self->last_modifiers = 0;
    return EDGE_OK;
}

// cppcheck-suppress constParameterPointer
edge_status_t zmk_behavior_shutdown(zmk_behavior_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

edge_status_t zmk_behavior_add_hold_tap(zmk_behavior_app_t *self, const zmk_ht_config_t *config,
                                        uint32_t hold_behavior_id, uint32_t hold_param,
                                        uint32_t tap_behavior_id, uint32_t tap_param,
                                        uint8_t *out_index) {
    if (self == NULL || config == NULL || self->hold_tap_count >= ZMK_MAX_HOLD_TAP) {
        return EDGE_EINVAL;
    }
    uint8_t idx = self->hold_tap_count++;
    zmk_ht_instance_t *ht = &self->hold_taps[idx];
    ht->config = *config;
    if (ht->config.tapping_term_ms == 0) {
        ht->config.tapping_term_ms = 200; /* Default 200ms */
    }
    ht->hold_behavior_id = hold_behavior_id;
    ht->hold_param1 = hold_param;
    ht->tap_behavior_id = tap_behavior_id;
    ht->tap_param1 = tap_param;

    if (out_index != NULL) {
        *out_index = idx;
    }
    return EDGE_OK;
}

edge_status_t zmk_behavior_add_tap_dance(zmk_behavior_app_t *self,
                                         const zmk_tap_dance_config_t *config, uint8_t *out_index) {
    if (self == NULL || config == NULL || self->tap_dance_count >= ZMK_MAX_TAP_DANCE ||
        config->binding_count == 0) {
        return EDGE_EINVAL;
    }
    uint8_t idx = self->tap_dance_count++;
    zmk_tap_dance_instance_t *td = &self->tap_dances[idx];
    td->config = *config;
    if (td->config.tapping_term_ms == 0) {
        td->config.tapping_term_ms = 200;
    }
    if (out_index != NULL) {
        *out_index = idx;
    }
    return EDGE_OK;
}

edge_status_t zmk_behavior_add_mod_morph(zmk_behavior_app_t *self,
                                         const zmk_mod_morph_config_t *config, uint8_t *out_index) {
    if (self == NULL || config == NULL || self->mod_morph_count >= ZMK_MAX_MOD_MORPH) {
        return EDGE_EINVAL;
    }
    uint8_t idx = self->mod_morph_count++;
    self->mod_morphs[idx] = *config;
    if (out_index != NULL) {
        *out_index = idx;
    }
    return EDGE_OK;
}

edge_status_t zmk_behavior_add_macro(zmk_behavior_app_t *self, const zmk_macro_config_t *config,
                                     uint8_t *out_index) {
    if (self == NULL || config == NULL || self->macro_count >= ZMK_MAX_MACROS) {
        return EDGE_EINVAL;
    }
    uint8_t idx = self->macro_count++;
    self->macros[idx] = *config;
    if (out_index != NULL) {
        *out_index = idx;
    }
    return EDGE_OK;
}

static void notify_hold_taps_on_other_key(zmk_behavior_app_t *self, bool other_pressed,
                                          uint32_t timestamp_ms) {
    for (uint8_t i = 0; i < self->hold_tap_count; i++) {
        zmk_ht_instance_t *ht = &self->hold_taps[i];
        if (ht->active && !ht->is_held) {
            ht->interrupted = true;
            if (!other_pressed) {
                ht->other_key_released = true;
            }
            if (ht->config.flavor == ZMK_HT_HOLD_PREFERRED ||
                ht->config.flavor == ZMK_HT_TAP_UNLESS_INTERRUPTED) {
                ht->is_held = true;
                zmk_behavior_invoke(self, (uint16_t)ht->hold_behavior_id, ht->hold_param1, 0, true,
                                    timestamp_ms);
            } else if (ht->config.flavor == ZMK_HT_BALANCED) {
                if (ht->other_key_released) {
                    ht->is_held = true;
                    zmk_behavior_invoke(self, (uint16_t)ht->hold_behavior_id, ht->hold_param1, 0,
                                        true, timestamp_ms);
                }
            }
        }
    }
}

static edge_status_t handle_key_press(zmk_behavior_app_t *self, uint8_t keycode, uint8_t modifiers,
                                      bool pressed, uint32_t timestamp_ms) {
    if (self->hid == NULL) {
        return EDGE_EINVAL;
    }

    notify_hold_taps_on_other_key(self, pressed, timestamp_ms);

    uint8_t eff_mods = modifiers;

    if (self->caps_word.active) {
        /* Alpha keys a-z (0x04..0x1D) get Left Shift (0x02) */
        if (keycode >= 0x04 && keycode <= 0x1D) {
            eff_mods |= 0x02;
        } else if (keycode == 0x2D || (keycode >= 0x1E && keycode <= 0x27)) {
            /* Numbers or minus/underscore keep Caps Word alive */
        } else {
            /* Any separator/whitespace key deactivates */
            self->caps_word.active = false;
        }
    }

    if (self->sticky_key.active) {
        eff_mods |= self->sticky_key.modifiers;
    }

    if (pressed) {
        self->last_keycode = keycode;
        self->last_modifiers = eff_mods;
        self->active_modifiers |= eff_mods;
        return self->hid->press_key(self->hid->self, keycode, eff_mods);
    } else {
        self->active_modifiers &= (uint8_t)~eff_mods;
        edge_status_t rc = self->hid->release_key(self->hid->self, keycode, eff_mods);
        if (self->sticky_key.active) {
            self->sticky_key.active = false;
        }
        return rc;
    }
}

edge_status_t zmk_behavior_invoke(zmk_behavior_app_t *self, uint16_t behavior_id, uint32_t param1,
                                  uint32_t param2, bool pressed, uint32_t timestamp_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_time_ms = timestamp_ms;

    switch (behavior_id) {
    case ZMK_BHV_KEY_PRESS:
        return handle_key_press(self, (uint8_t)param1, (uint8_t)param2, pressed, timestamp_ms);

    case ZMK_BHV_HOLD_TAP: {
        if (param1 >= self->hold_tap_count) {
            return EDGE_EINVAL;
        }
        zmk_ht_instance_t *ht = &self->hold_taps[param1];
        if (pressed) {
            /* Check quick tap */
            if (ht->config.quick_tap_ms > 0 && ht->has_previous_tap &&
                (timestamp_ms - ht->last_tap_time_ms) < ht->config.quick_tap_ms) {
                /* Double tap -> fire tap directly */
                zmk_behavior_invoke(self, (uint16_t)ht->tap_behavior_id, ht->tap_param1, 0, true,
                                    timestamp_ms);
                ht->is_tapped = true;
                return EDGE_OK;
            }
            ht->active = true;
            ht->is_held = false;
            ht->is_tapped = false;
            ht->interrupted = false;
            ht->other_key_released = false;
            ht->press_time_ms = timestamp_ms;
            return EDGE_OK;
        } else {
            /* Release */
            if (ht->is_tapped) {
                ht->is_tapped = false;
                return zmk_behavior_invoke(self, (uint16_t)ht->tap_behavior_id, ht->tap_param1, 0,
                                           false, timestamp_ms);
            }
            if (ht->active) {
                ht->active = false;
                if (ht->is_held) {
                    ht->is_held = false;
                    if (ht->config.retro_tap && !ht->interrupted) {
                        /* Retro tap: held past term but no other key pressed -> tap */
                        zmk_behavior_invoke(self, (uint16_t)ht->hold_behavior_id, ht->hold_param1,
                                            0, false, timestamp_ms);
                        ht->last_tap_time_ms = timestamp_ms;
                        ht->has_previous_tap = true;
                        zmk_behavior_invoke(self, (uint16_t)ht->tap_behavior_id, ht->tap_param1, 0,
                                            true, timestamp_ms);
                        return zmk_behavior_invoke(self, (uint16_t)ht->tap_behavior_id,
                                                   ht->tap_param1, 0, false, timestamp_ms);
                    }
                    return zmk_behavior_invoke(self, (uint16_t)ht->hold_behavior_id,
                                               ht->hold_param1, 0, false, timestamp_ms);
                } else {
                    /* Tapped */
                    ht->last_tap_time_ms = timestamp_ms;
                    ht->has_previous_tap = true;
                    zmk_behavior_invoke(self, (uint16_t)ht->tap_behavior_id, ht->tap_param1, 0,
                                        true, timestamp_ms);
                    return zmk_behavior_invoke(self, (uint16_t)ht->tap_behavior_id, ht->tap_param1,
                                               0, false, timestamp_ms);
                }
            }
            return EDGE_OK;
        }
    }

    case ZMK_BHV_TAP_DANCE: {
        if (param1 >= self->tap_dance_count) {
            return EDGE_EINVAL;
        }
        zmk_tap_dance_instance_t *td = &self->tap_dances[param1];
        if (pressed) {
            td->active = true;
            td->tap_count++;
            td->last_tap_time_ms = timestamp_ms;
            if (td->tap_count >= td->config.binding_count) {
                /* Max taps reached -> fire latest binding */
                uint8_t idx = td->tap_count - 1;
                zmk_behavior_invoke(self, td->config.bindings[idx].behavior_id,
                                    td->config.bindings[idx].param1, 0, true, timestamp_ms);
            }
        } else {
            if (td->tap_count >= td->config.binding_count) {
                uint8_t idx = td->tap_count - 1;
                zmk_behavior_invoke(self, td->config.bindings[idx].behavior_id,
                                    td->config.bindings[idx].param1, 0, false, timestamp_ms);
                td->active = false;
                td->tap_count = 0;
            }
        }
        return EDGE_OK;
    }

    case ZMK_BHV_STICKY_KEY: {
        if (pressed) {
            self->sticky_key.active = true;
            self->sticky_key.modifiers = (uint8_t)param1;
            self->sticky_key.activate_time_ms = timestamp_ms;
            self->sticky_key.timeout_ms = (param2 > 0) ? (uint16_t)param2 : 1000;
        }
        return EDGE_OK;
    }

    case ZMK_BHV_CAPS_WORD: {
        if (pressed) {
            self->caps_word.active = !self->caps_word.active;
        }
        return EDGE_OK;
    }

    case ZMK_BHV_KEY_REPEAT: {
        if (self->last_keycode != 0) {
            return handle_key_press(self, self->last_keycode, self->last_modifiers, pressed,
                                    timestamp_ms);
        }
        return EDGE_OK;
    }

    case ZMK_BHV_KEY_TOGGLE: {
        if (pressed) {
            uint8_t keycode = (uint8_t)param1;
            bool new_state = !self->key_toggle_states[keycode];
            self->key_toggle_states[keycode] = new_state;
            return handle_key_press(self, keycode, (uint8_t)param2, new_state, timestamp_ms);
        }
        return EDGE_OK;
    }

    case ZMK_BHV_MOD_MORPH: {
        if (param1 >= self->mod_morph_count) {
            return EDGE_EINVAL;
        }
        const zmk_mod_morph_config_t *morph = &self->mod_morphs[param1];
        uint8_t effective_mods = self->active_modifiers;
        if (self->sticky_key.active) {
            effective_mods |= self->sticky_key.modifiers;
        }
        bool morphed = (effective_mods & morph->trigger_mods_mask) != 0;
        uint8_t kc = morphed ? morph->morphed_keycode : morph->default_keycode;
        uint8_t mods = morphed ? morph->morphed_mods : morph->default_mods;
        return handle_key_press(self, kc, mods, pressed, timestamp_ms);
    }

    case ZMK_BHV_MACRO: {
        if (param1 >= self->macro_count || !pressed) {
            return EDGE_OK;
        }
        const zmk_macro_config_t *macro = &self->macros[param1];
        uint32_t step_time = timestamp_ms;
        for (uint8_t s = 0; s < macro->step_count; s++) {
            const zmk_macro_step_t *st = &macro->steps[s];
            if (st->action == ZMK_MACRO_ACTION_PRESS) {
                handle_key_press(self, st->keycode, st->modifiers, true, step_time);
            } else if (st->action == ZMK_MACRO_ACTION_RELEASE) {
                handle_key_press(self, st->keycode, st->modifiers, false, step_time);
            } else if (st->action == ZMK_MACRO_ACTION_TAP) {
                uint16_t wait_duration = (st->wait_ms > 0) ? st->wait_ms : macro->default_wait_ms;
                handle_key_press(self, st->keycode, st->modifiers, true, step_time);
                step_time += wait_duration;
                handle_key_press(self, st->keycode, st->modifiers, false, step_time);
            } else if (st->action == ZMK_MACRO_ACTION_WAIT) {
                uint16_t wait_duration = (st->wait_ms > 0) ? st->wait_ms : macro->default_wait_ms;
                step_time += wait_duration;
            }
        }
        self->current_time_ms = step_time;
        return EDGE_OK;
    }

    case ZMK_BHV_MOUSE_KEY: {
        if (self->hid == NULL) {
            return EDGE_EINVAL;
        }
        if (pressed) {
            return self->hid->press_mouse_button(self->hid->self, (uint8_t)param1);
        } else {
            return self->hid->release_mouse_button(self->hid->self, (uint8_t)param1);
        }
    }

    case ZMK_BHV_MO:
    case ZMK_BHV_TOG:
    case ZMK_BHV_TO: {
        if (self->keymap == NULL) {
            return EDGE_OK;
        }
        if (behavior_id == ZMK_BHV_MO) {
            return pressed ? self->keymap->layer_activate(self->keymap->self, (uint8_t)param1)
                           : self->keymap->layer_deactivate(self->keymap->self, (uint8_t)param1);
        } else if (behavior_id == ZMK_BHV_TOG && pressed) {
            return self->keymap->layer_toggle(self->keymap->self, (uint8_t)param1);
        } else if (behavior_id == ZMK_BHV_TO && pressed) {
            return self->keymap->layer_to(self->keymap->self, (uint8_t)param1);
        }
        return EDGE_OK;
    }

    default:
        return EDGE_OK;
    }
}

edge_status_t zmk_behavior_tick(zmk_behavior_app_t *self, uint32_t timestamp_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_time_ms = timestamp_ms;

    /* Check hold-taps */
    for (uint8_t i = 0; i < self->hold_tap_count; i++) {
        zmk_ht_instance_t *ht = &self->hold_taps[i];
        if (ht->active && !ht->is_held) {
            if ((timestamp_ms - ht->press_time_ms) >= ht->config.tapping_term_ms) {
                if (ht->config.flavor != ZMK_HT_TAP_UNLESS_INTERRUPTED || ht->interrupted) {
                    /* Expired -> promote to hold */
                    ht->is_held = true;
                    zmk_behavior_invoke(self, (uint16_t)ht->hold_behavior_id, ht->hold_param1, 0,
                                        true, timestamp_ms);
                }
            }
        }
    }

    /* Check tap dances */
    for (uint8_t i = 0; i < self->tap_dance_count; i++) {
        zmk_tap_dance_instance_t *td = &self->tap_dances[i];
        if (td->active && td->tap_count > 0 && td->tap_count < td->config.binding_count) {
            if ((timestamp_ms - td->last_tap_time_ms) >= td->config.tapping_term_ms) {
                /* Expired -> fire taps */
                uint8_t idx = td->tap_count - 1;
                zmk_behavior_invoke(self, td->config.bindings[idx].behavior_id,
                                    td->config.bindings[idx].param1, 0, true, timestamp_ms);
                zmk_behavior_invoke(self, td->config.bindings[idx].behavior_id,
                                    td->config.bindings[idx].param1, 0, false, timestamp_ms);
                td->active = false;
                td->tap_count = 0;
            }
        }
    }

    /* Check sticky key timeout */
    if (self->sticky_key.active) {
        if ((timestamp_ms - self->sticky_key.activate_time_ms) >= self->sticky_key.timeout_ms) {
            self->sticky_key.active = false;
        }
    }

    return EDGE_OK;
}
