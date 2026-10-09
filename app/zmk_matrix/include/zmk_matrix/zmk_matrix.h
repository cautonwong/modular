#ifndef ZMK_MATRIX_H
#define ZMK_MATRIX_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_matrix/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_MATRIX_MAX_POSITIONS 128u

typedef struct zmk_matrix_transform {
    uint8_t rows;
    uint8_t cols;
    /* Map from (row, col) to linear position index. 0xFF = unused */
    uint8_t map[16][16];
} zmk_matrix_transform_t;

typedef struct zmk_matrix_app {
    edge_module_t module;
    const zmk_matrix_hw_if_t *hw;
    const zmk_matrix_event_sink_if_t *sink;
    zmk_matrix_transform_t transform;

    bool position_states[ZMK_MATRIX_MAX_POSITIONS];
    uint32_t last_scan_time_ms;
    uint32_t events_emitted;
} zmk_matrix_app_t;

void zmk_matrix_construct(zmk_matrix_app_t *self, uint32_t module_id, uint32_t priority,
                          const zmk_matrix_hw_if_t *hw, const zmk_matrix_event_sink_if_t *sink,
                          const zmk_matrix_transform_t *transform);

edge_status_t zmk_matrix_init(zmk_matrix_app_t *self);
edge_status_t zmk_matrix_shutdown(zmk_matrix_app_t *self);

/* Handle debounce callback from kscan driver */
edge_status_t zmk_matrix_on_key_state_change(zmk_matrix_app_t *self, uint8_t row, uint8_t col,
                                             bool pressed, uint32_t timestamp_ms);

bool zmk_matrix_is_position_pressed(const zmk_matrix_app_t *self, uint32_t position);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_MATRIX_H */
