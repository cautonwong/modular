#ifndef ZMK_POINTING_PORTS_H
#define ZMK_POINTING_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_pointing_hid_if {
    void *self;
    edge_status_t (*report_motion)(void *self, int16_t dx, int16_t dy, int8_t v_scroll,
                                   int8_t h_scroll);
    edge_status_t (*report_buttons)(void *self, uint8_t buttons_mask);
} zmk_pointing_hid_if_t;

#ifdef __cplusplus
}
#endif

#endif /* ZMK_POINTING_PORTS_H */
