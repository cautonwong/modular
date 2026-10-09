#ifndef ZMK_PM_PORTS_H
#define ZMK_PM_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zmk_pm_sink_if {
    void *self;
    edge_status_t (*on_activity_state_changed)(void *self, uint8_t state);
} zmk_pm_sink_if_t;

#ifdef __cplusplus
}
#endif

#endif /* ZMK_PM_PORTS_H */
