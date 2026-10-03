#ifndef INFRA_KSCAN_H
#define INFRA_KSCAN_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KSCAN_MAX_ROWS 16u
#define KSCAN_MAX_COLS 16u
#define KSCAN_MAX_KEYS (KSCAN_MAX_ROWS * KSCAN_MAX_COLS)

typedef struct kscan_matrix_config {
    uint8_t rows;
    uint8_t cols;
    uint32_t debounce_press_ms;
    uint32_t debounce_release_ms;
} kscan_matrix_config_t;

typedef void (*kscan_callback_fn)(void *user_data, uint8_t row, uint8_t col, bool pressed);

typedef struct kscan_debounce_state {
    uint32_t last_change_time_ms;
    bool stable_state;
    bool candidate_state;
} kscan_debounce_state_t;

typedef struct kscan_matrix {
    kscan_matrix_config_t config;
    kscan_debounce_state_t debounce[KSCAN_MAX_ROWS][KSCAN_MAX_COLS];
    bool raw_matrix[KSCAN_MAX_ROWS][KSCAN_MAX_COLS];

    kscan_callback_fn callback;
    void *user_data;
    uint32_t current_time_ms;
} kscan_matrix_t;

edge_status_t kscan_matrix_init(kscan_matrix_t *self, const kscan_matrix_config_t *config,
                                kscan_callback_fn callback, void *user_data);

/* Feed raw pin sample for a matrix point */
edge_status_t kscan_matrix_feed_raw(kscan_matrix_t *self, uint8_t row, uint8_t col, bool raw_level,
                                    uint32_t timestamp_ms);

/* Periodic scan tick to process debounce timers */
edge_status_t kscan_matrix_process_debounce(kscan_matrix_t *self, uint32_t timestamp_ms);

/* Query stable state of a key */
bool kscan_matrix_is_pressed(const kscan_matrix_t *self, uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

#endif /* INFRA_KSCAN_H */
