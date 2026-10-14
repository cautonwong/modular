#ifndef ZMK_WPM_PORTS_H
#define ZMK_WPM_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef ZMK_WPM_MODULE_ID
#define ZMK_WPM_MODULE_ID 0x2A00u
#endif

#ifndef EDGE_MOD_ZMK_WPM
#define EDGE_MOD_ZMK_WPM ZMK_WPM_MODULE_ID
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_wpm_sink_if {
    void *self;
    edge_status_t (*on_wpm_state_changed)(void *self, uint8_t wpm);
} zmk_wpm_sink_if_t;

#ifdef __cplusplus
}
#endif

#endif /* ZMK_WPM_PORTS_H */
