#include "ble_motion/ble_motion.h"
#include "edge/events.h"
#include "edge/modules.h"

static edge_status_t motion_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t motion_on_event(edge_module_t *module, const edge_event_t *event) {
    ble_motion_t *self = (ble_motion_t *)module->private_data;
    if (self == NULL || event == NULL) {
        return EDGE_EINVAL;
    }
    if (event->id == EDGE_EVT_WATCH_STEP_COUNT) {
        return ble_motion_on_step_count(self, event->arg0);
    }
    return EDGE_OK;
}

void ble_motion_init(ble_motion_t *self, const ble_motion_notify_port_t *notify_port,
                     edge_event_sink_t *event_sink) {
    ble_motion_construct(self, EDGE_MOD_BLE_MOTION, 2u, notify_port, event_sink);
}

void ble_motion_construct(ble_motion_t *self, uint32_t module_id, uint32_t priority,
                          const ble_motion_notify_port_t *notify_port,
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
        .poll = motion_poll,
        .on_event = motion_on_event,
        .power_off = NULL,
        .private_data = self,
    };
    if (notify_port != NULL) {
        self->notify_port = *notify_port;
    }
    self->event_sink = event_sink;
}

void ble_motion_set_step_notify_enabled(ble_motion_t *self, bool enabled) {
    if (self == NULL) {
        return;
    }
    self->step_count_notify_enabled = enabled;
}

void ble_motion_set_motion_notify_enabled(ble_motion_t *self, bool enabled) {
    if (self == NULL) {
        return;
    }
    self->motion_values_notify_enabled = enabled;
}

edge_status_t ble_motion_on_step_count(ble_motion_t *self, uint32_t step_count) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->last_step_count = step_count;
    if (self->step_count_notify_enabled && self->notify_port.notify_step_count != NULL) {
        return self->notify_port.notify_step_count(self->notify_port.self, step_count);
    }
    return EDGE_OK;
}

edge_status_t ble_motion_on_motion_values(ble_motion_t *self, int16_t x, int16_t y, int16_t z) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->last_x = x;
    self->last_y = y;
    self->last_z = z;

    if (self->event_sink != NULL) {
        edge_event_t ev = {
            .id = EDGE_EVT_WATCH_MOTION_STREAM,
            .source = EDGE_MOD_BLE_MOTION,
            .arg0 = (uint32_t)(uint16_t)x | ((uint32_t)(uint16_t)y << 16u),
            .arg1 = (uint32_t)(uint16_t)z,
            .timestamp = 0u,
        };
        edge_event_sink_push_isr(self->event_sink, &ev);
    }

    if (self->motion_values_notify_enabled && self->notify_port.notify_motion_values != NULL) {
        return self->notify_port.notify_motion_values(self->notify_port.self, x, y, z);
    }
    return EDGE_OK;
}

edge_status_t ble_motion_encode_steps(uint32_t step_count, uint8_t *out_buf, size_t buf_len) {
    if (out_buf == NULL || buf_len < 4u) {
        return EDGE_EINVAL;
    }
    out_buf[0] = (uint8_t)(step_count & 0xFFu);
    out_buf[1] = (uint8_t)((step_count >> 8u) & 0xFFu);
    out_buf[2] = (uint8_t)((step_count >> 16u) & 0xFFu);
    out_buf[3] = (uint8_t)((step_count >> 24u) & 0xFFu);
    return EDGE_OK;
}

edge_status_t ble_motion_encode_values(int16_t x, int16_t y, int16_t z, uint8_t *out_buf,
                                       size_t buf_len) {
    if (out_buf == NULL || buf_len < 6u) {
        return EDGE_EINVAL;
    }
    uint16_t ux = (uint16_t)x;
    uint16_t uy = (uint16_t)y;
    uint16_t uz = (uint16_t)z;

    out_buf[0] = (uint8_t)(ux & 0xFFu);
    out_buf[1] = (uint8_t)((ux >> 8u) & 0xFFu);
    out_buf[2] = (uint8_t)(uy & 0xFFu);
    out_buf[3] = (uint8_t)((uy >> 8u) & 0xFFu);
    out_buf[4] = (uint8_t)(uz & 0xFFu);
    out_buf[5] = (uint8_t)((uz >> 8u) & 0xFFu);
    return EDGE_OK;
}
