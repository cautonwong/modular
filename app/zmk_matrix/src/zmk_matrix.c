#include "zmk_matrix/zmk_matrix.h"

static edge_status_t zmk_matrix_poll(edge_module_t *module) {
    zmk_matrix_app_t *self = (zmk_matrix_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->hw != NULL && self->hw->scan_raw != NULL) {
        return self->hw->scan_raw(self->hw->self, self->last_scan_time_ms);
    }
    return EDGE_OK;
}

static edge_status_t zmk_matrix_power_off(edge_module_t *module) {
    zmk_matrix_app_t *self = (zmk_matrix_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->position_states); ++i)
        self->position_states[i] = false;
    return EDGE_OK;
}

void zmk_matrix_construct(zmk_matrix_app_t *self, uint32_t module_id, uint32_t priority,
                          const zmk_matrix_hw_if_t *hw, const zmk_matrix_event_sink_if_t *sink,
                          const zmk_matrix_transform_t *transform) {
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
        .poll = zmk_matrix_poll,
        .on_event = NULL,
        .power_off = zmk_matrix_power_off,
        .private_data = self,
    };
    self->hw = hw;
    self->sink = sink;
    if (transform != NULL) {
        self->transform = *transform;
    } else {
        /* Default 1:1 identity map for up to 16x16 */
        self->transform.rows = 16;
        self->transform.cols = 16;
        for (uint8_t r = 0; r < 16; r++) {
            for (uint8_t c = 0; c < 16; c++) {
                uint32_t pos = (uint32_t)r * 16u + c;
                self->transform.map[r][c] = (pos < ZMK_MATRIX_MAX_POSITIONS) ? (uint8_t)pos : 0xFFu;
            }
        }
    }
}

edge_status_t zmk_matrix_init(zmk_matrix_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->position_states); ++i)
        self->position_states[i] = false;
    self->last_scan_time_ms = 0;
    self->events_emitted = 0;
    return EDGE_OK;
}

edge_status_t zmk_matrix_shutdown(zmk_matrix_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->position_states); ++i)
        self->position_states[i] = false;
    return EDGE_OK;
}

edge_status_t zmk_matrix_on_key_state_change(zmk_matrix_app_t *self, uint8_t row, uint8_t col,
                                             bool pressed, uint32_t timestamp_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (row >= 16 || col >= 16) {
        return EDGE_EINVAL;
    }
    uint8_t pos = self->transform.map[row][col];
    if (pos == 0xFFu || pos >= ZMK_MATRIX_MAX_POSITIONS) {
        /* Unmapped position */
        return EDGE_OK;
    }

    self->position_states[pos] = pressed;
    self->last_scan_time_ms = timestamp_ms;
    self->events_emitted++;

    if (self->sink != NULL && self->sink->post_position_event != NULL) {
        return self->sink->post_position_event(self->sink->self, pos, pressed, timestamp_ms);
    }
    return EDGE_OK;
}

bool zmk_matrix_is_position_pressed(const zmk_matrix_app_t *self, uint32_t position) {
    if (self == NULL || position >= ZMK_MATRIX_MAX_POSITIONS) {
        return false;
    }
    return self->position_states[position];
}
