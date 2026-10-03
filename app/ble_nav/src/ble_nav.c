#include <stddef.h>
#include <stdint.h>
static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}
#include "ble_nav/ble_nav.h"
#include "edge/events.h"
#include "edge/modules.h"

static edge_status_t nav_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void ble_nav_init(ble_nav_t *self, edge_event_sink_t *event_sink) {
    ble_nav_construct(self, EDGE_MOD_BLE_NAV, 2u, event_sink);
}

void ble_nav_construct(ble_nav_t *self, uint32_t module_id, uint32_t priority,
                       edge_event_sink_t *event_sink) {
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
        .poll = nav_poll,
        .on_event = NULL,
        .power_off = NULL,
        .private_data = self,
    };
    self->event_sink = event_sink;
}

static void emit_updated_event(ble_nav_t *self) {
    if (self != NULL && self->event_sink != NULL) {
        edge_event_t ev = {
            .id = EDGE_EVT_WATCH_NAV_UPDATED,
            .source = EDGE_MOD_BLE_NAV,
            .arg0 = (uint32_t)self->info.progress,
            .arg1 = 0u,
            .timestamp = 0u,
        };
        edge_event_sink_push_isr(self->event_sink, &ev);
    }
}

void ble_nav_set_flag(ble_nav_t *self, const char *flag, size_t len) {
    if (self == NULL) {
        return;
    }
    size_t copy_len = (len < BLE_NAV_FLAG_MAX_LEN) ? len : BLE_NAV_FLAG_MAX_LEN;
    copy_bytes(self->info.flag, flag, copy_len);
    self->info.flag[copy_len] = '\0';
    self->info.valid = true;
    emit_updated_event(self);
}

void ble_nav_set_narrative(ble_nav_t *self, const char *narrative, size_t len) {
    if (self == NULL) {
        return;
    }
    size_t copy_len = (len < BLE_NAV_NARRATIVE_MAX_LEN) ? len : BLE_NAV_NARRATIVE_MAX_LEN;
    copy_bytes(self->info.narrative, narrative, copy_len);
    self->info.narrative[copy_len] = '\0';
    self->info.valid = true;
    emit_updated_event(self);
}

void ble_nav_set_man_dist(ble_nav_t *self, const char *dist, size_t len) {
    if (self == NULL) {
        return;
    }
    size_t copy_len = (len < BLE_NAV_MANDIST_MAX_LEN) ? len : BLE_NAV_MANDIST_MAX_LEN;
    copy_bytes(self->info.man_dist, dist, copy_len);
    self->info.man_dist[copy_len] = '\0';
    self->info.valid = true;
    emit_updated_event(self);
}

void ble_nav_set_progress(ble_nav_t *self, uint8_t progress) {
    if (self == NULL) {
        return;
    }
    self->info.progress = (progress <= 100u) ? progress : 100u;
    self->info.valid = true;
    emit_updated_event(self);
}

bool ble_nav_get_info(const ble_nav_t *self, ble_nav_info_t *out_info) {
    if (self == NULL || !self->info.valid) {
        return false;
    }
    if (out_info != NULL) {
        *out_info = self->info;
    }
    return true;
}

void ble_nav_clear(ble_nav_t *self) {
    if (self == NULL) {
        return;
    }
    self->info = (__typeof__(self->info)){0};
    emit_updated_event(self);
}
