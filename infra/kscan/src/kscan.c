#include "kscan/kscan.h"

edge_status_t kscan_matrix_init(kscan_matrix_t *self, const kscan_matrix_config_t *config,
                                kscan_callback_fn callback, void *user_data) {
    if (self == NULL || config == NULL) {
        return EDGE_EINVAL;
    }
    if (config->rows == 0u || config->rows > KSCAN_MAX_ROWS || config->cols == 0u ||
        config->cols > KSCAN_MAX_COLS) {
        return EDGE_EINVAL;
    }

    *self = (__typeof__(*self)){0};
    self->config = *config;
    self->callback = callback;
    self->user_data = user_data;

    return EDGE_OK;
}

edge_status_t kscan_matrix_feed_raw(kscan_matrix_t *self, uint8_t row, uint8_t col, bool raw_level,
                                    uint32_t timestamp_ms) {
    if (self == NULL || row >= self->config.rows || col >= self->config.cols) {
        return EDGE_EINVAL;
    }

    self->current_time_ms = timestamp_ms;
    self->raw_matrix[row][col] = raw_level;

    kscan_debounce_state_t *deb = &self->debounce[row][col];
    if (deb->candidate_state != raw_level) {
        deb->candidate_state = raw_level;
        deb->last_change_time_ms = timestamp_ms;
    }

    return EDGE_OK;
}

edge_status_t kscan_matrix_process_debounce(kscan_matrix_t *self, uint32_t timestamp_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->current_time_ms = timestamp_ms;

    for (uint8_t r = 0; r < self->config.rows; r++) {
        for (uint8_t c = 0; c < self->config.cols; c++) {
            kscan_debounce_state_t *deb = &self->debounce[r][c];
            if (deb->candidate_state != deb->stable_state) {
                uint32_t hold_time = timestamp_ms - deb->last_change_time_ms;
                uint32_t threshold = deb->candidate_state ? self->config.debounce_press_ms
                                                          : self->config.debounce_release_ms;

                if (hold_time >= threshold) {
                    deb->stable_state = deb->candidate_state;
                    if (self->callback != NULL) {
                        self->callback(self->user_data, r, c, deb->stable_state);
                    }
                }
            }
        }
    }

    return EDGE_OK;
}

bool kscan_matrix_is_pressed(const kscan_matrix_t *self, uint8_t row, uint8_t col) {
    if (self == NULL || row >= self->config.rows || col >= self->config.cols) {
        return false;
    }
    return self->debounce[row][col].stable_state;
}
