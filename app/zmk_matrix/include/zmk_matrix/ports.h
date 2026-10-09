#ifndef ZMK_MATRIX_PORTS_H
#define ZMK_MATRIX_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_matrix_hw_if {
    void *self;
    edge_status_t (*scan_raw)(void *self, uint32_t timestamp_ms);
} zmk_matrix_hw_if_t;

typedef struct zmk_matrix_event_sink_if {
    void *self;
    edge_status_t (*post_position_event)(void *self, uint32_t position, bool pressed,
                                         uint32_t timestamp_ms);
} zmk_matrix_event_sink_if_t;

#ifdef __cplusplus
}
#endif

#endif /* ZMK_MATRIX_PORTS_H */
