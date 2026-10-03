#ifndef ZMK_WPM_H
#define ZMK_WPM_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "zmk_wpm/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_WPM_WINDOW_BUCKETS 10u
#define ZMK_WPM_BUCKET_DURATION_MS 500u /* 10 * 500ms = 5000ms (5 second sliding window) */

typedef struct zmk_wpm_app {
    edge_module_t module;
    const zmk_wpm_sink_if_t *sink;

    uint8_t current_wpm;
    uint16_t buckets[ZMK_WPM_WINDOW_BUCKETS];
    uint8_t current_bucket_idx;
    uint32_t last_bucket_time_ms;
} zmk_wpm_app_t;

void zmk_wpm_construct(zmk_wpm_app_t *self, uint32_t module_id, uint32_t priority,
                       const zmk_wpm_sink_if_t *sink);

edge_status_t zmk_wpm_init(zmk_wpm_app_t *self);
edge_status_t zmk_wpm_shutdown(zmk_wpm_app_t *self);

void zmk_wpm_record_keystroke(zmk_wpm_app_t *self, uint32_t timestamp_ms);
edge_status_t zmk_wpm_tick(zmk_wpm_app_t *self, uint32_t timestamp_ms);
uint8_t zmk_wpm_get_current_wpm(const zmk_wpm_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_WPM_H */
