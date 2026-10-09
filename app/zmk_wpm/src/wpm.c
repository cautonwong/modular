#include "zmk_wpm/wpm.h"

static void recalculate_wpm(zmk_wpm_app_t *self) {
    uint32_t total_strokes = 0;
    for (uint8_t i = 0; i < ZMK_WPM_WINDOW_BUCKETS; i++) {
        total_strokes += self->buckets[i];
    }
    /* 5 seconds window: total_strokes in 5s.
       Strokes per minute = total_strokes * 12.
       Words per minute = (total_strokes * 12) / 5 = (total_strokes * 12) / 5 */
    uint32_t wpm = (total_strokes * 12u) / 5u;
    uint8_t new_wpm = (wpm > 255u) ? 255u : (uint8_t)wpm;

    if (new_wpm != self->current_wpm) {
        self->current_wpm = new_wpm;
        if (self->sink != NULL && self->sink->on_wpm_state_changed != NULL) {
            self->sink->on_wpm_state_changed(self->sink->self, new_wpm);
        }
    }
}

static edge_status_t zmk_wpm_poll(edge_module_t *module) {
    const zmk_wpm_app_t *self = (const zmk_wpm_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return EDGE_OK;
}

static edge_status_t zmk_wpm_power_off(edge_module_t *module) {
    zmk_wpm_app_t *self = (zmk_wpm_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->buckets) / sizeof(self->buckets[0]); ++i)
        self->buckets[i] = 0;
    self->current_wpm = 0;
    return EDGE_OK;
}

void zmk_wpm_construct(zmk_wpm_app_t *self, uint32_t module_id, uint32_t priority,
                       const zmk_wpm_sink_if_t *sink) {
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
        .poll = zmk_wpm_poll,
        .on_event = NULL,
        .power_off = zmk_wpm_power_off,
        .private_data = self,
    };
    self->sink = sink;
    self->current_wpm = 0;
}

edge_status_t zmk_wpm_init(zmk_wpm_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->buckets) / sizeof(self->buckets[0]); ++i)
        self->buckets[i] = 0;
    self->current_wpm = 0;
    self->current_bucket_idx = 0;
    self->last_bucket_time_ms = 0;
    return EDGE_OK;
}

edge_status_t zmk_wpm_shutdown(zmk_wpm_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    for (size_t i = 0; i < sizeof(self->buckets) / sizeof(self->buckets[0]); ++i)
        self->buckets[i] = 0;
    self->current_wpm = 0;
    return EDGE_OK;
}

void zmk_wpm_record_keystroke(zmk_wpm_app_t *self, uint32_t timestamp_ms) {
    if (self == NULL) {
        return;
    }
    zmk_wpm_tick(self, timestamp_ms);
    self->buckets[self->current_bucket_idx]++;
    recalculate_wpm(self);
}

edge_status_t zmk_wpm_tick(zmk_wpm_app_t *self, uint32_t timestamp_ms) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (self->last_bucket_time_ms == 0) {
        self->last_bucket_time_ms = timestamp_ms;
        return EDGE_OK;
    }

    uint32_t elapsed = timestamp_ms - self->last_bucket_time_ms;
    uint32_t buckets_to_advance = elapsed / ZMK_WPM_BUCKET_DURATION_MS;

    if (buckets_to_advance > 0) {
        if (buckets_to_advance > ZMK_WPM_WINDOW_BUCKETS) {
            buckets_to_advance = ZMK_WPM_WINDOW_BUCKETS;
        }
        for (uint32_t i = 0; i < buckets_to_advance; i++) {
            self->current_bucket_idx = (self->current_bucket_idx + 1) % ZMK_WPM_WINDOW_BUCKETS;
            self->buckets[self->current_bucket_idx] = 0; /* Clear newly entered bucket */
        }
        self->last_bucket_time_ms += buckets_to_advance * ZMK_WPM_BUCKET_DURATION_MS;
        recalculate_wpm(self);
    }
    return EDGE_OK;
}

uint8_t zmk_wpm_get_current_wpm(const zmk_wpm_app_t *self) {
    if (self == NULL) {
        return 0;
    }
    return self->current_wpm;
}
