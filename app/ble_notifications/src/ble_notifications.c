#include <stddef.h>
#include <stdint.h>
static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}
#include "ble_notifications/ble_notifications.h"
#include "edge/events.h"
#include "edge/modules.h"

static edge_status_t notif_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void ble_notifications_init(ble_notifications_t *self,
                            const ble_notifications_call_port_t *call_port,
                            edge_event_sink_t *event_sink) {
    ble_notifications_construct(self, EDGE_MOD_BLE_NOTIFICATIONS, 2u, call_port, event_sink);
}

void ble_notifications_construct(ble_notifications_t *self, uint32_t module_id, uint32_t priority,
                                 const ble_notifications_call_port_t *call_port,
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
        .poll = notif_poll,
        .on_event = NULL,
        .power_off = NULL,
        .private_data = self,
    };
    if (call_port != NULL) {
        self->call_port = *call_port;
    }
    self->event_sink = event_sink;
    self->next_id = 1u;
}

uint8_t ble_notifications_push(ble_notifications_t *self, uint8_t category, const char *msg,
                               size_t len) {
    if (self == NULL || msg == NULL || len == 0) {
        return 0;
    }

    uint8_t id = self->next_id++;
    if (self->next_id == 0) {
        self->next_id = 1u;
    }

    /* Ring buffer advance */
    uint8_t slot;
    if (self->count == 0) {
        slot = 0;
        self->head_idx = 0;
        self->count = 1;
    } else {
        self->head_idx = (self->head_idx + 1u) % BLE_NOTIF_MAX_COUNT;
        slot = self->head_idx;
        if (self->count < BLE_NOTIF_MAX_COUNT) {
            self->count++;
        }
    }

    ble_notification_item_t *item = &self->items[slot];
    item->id = id;
    item->category = category;

    size_t copy_len = (len < BLE_NOTIF_MESSAGE_MAX_LEN) ? len : BLE_NOTIF_MESSAGE_MAX_LEN;
    copy_bytes(item->message, msg, copy_len);
    item->message[copy_len] = '\0';
    item->message_len = (uint8_t)copy_len;
    item->valid = true;

    if (self->event_sink != NULL) {
        edge_event_t ev = {
            .id = EDGE_EVT_WATCH_NOTIF_NEW,
            .source = EDGE_MOD_BLE_NOTIFICATIONS,
            .arg0 = ((uint32_t)category << 8u) | (uint32_t)id,
            .arg1 = 0u,
            .timestamp = 0u,
        };
        edge_event_sink_push_isr(self->event_sink, &ev);
    }

    return id;
}

edge_status_t ble_notifications_process_ans_packet(ble_notifications_t *self, const uint8_t *data,
                                                   size_t len) {
    if (self == NULL || data == NULL || len <= 3u) {
        return EDGE_EINVAL;
    }

    uint8_t ans_category = data[0];
    uint8_t mapped_category;

    /* Map ANS Category ID */
    switch (ans_category) {
    case 3: /* Call */
        mapped_category = BLE_NOTIF_CAT_INCOMING_CALL;
        break;
    case 4: /* Missed Call */
        mapped_category = BLE_NOTIF_CAT_MISSED_CALL;
        break;
    case 5: /* SMS */
        mapped_category = BLE_NOTIF_CAT_SMS;
        break;
    case 1: /* Email */
        mapped_category = BLE_NOTIF_CAT_EMAIL;
        break;
    case 2: /* News */
        mapped_category = BLE_NOTIF_CAT_NEWS;
        break;
    case 6: /* Voice Mail */
        mapped_category = BLE_NOTIF_CAT_VOICE_MAIL;
        break;
    case 7: /* Schedule */
        mapped_category = BLE_NOTIF_CAT_SCHEDULE;
        break;
    case 8: /* High Priority */
        mapped_category = BLE_NOTIF_CAT_HIGH_PRIORITY;
        break;
    case 9: /* Instant Message */
        mapped_category = BLE_NOTIF_CAT_INSTANT_MESSAGE;
        break;
    default:
        mapped_category = BLE_NOTIF_CAT_SIMPLE_ALERT;
        break;
    }

    const char *msg_payload = (const char *)&data[3];
    size_t msg_len = len - 3u;

    uint8_t id = ble_notifications_push(self, mapped_category, msg_payload, msg_len);
    return (id > 0) ? EDGE_OK : EDGE_EINVAL;
}

uint8_t ble_notifications_count(const ble_notifications_t *self) {
    return (self != NULL) ? self->count : 0u;
}

bool ble_notifications_get_by_id(const ble_notifications_t *self, uint8_t id,
                                 ble_notification_item_t *out_item) {
    if (self == NULL || id == 0 || self->count == 0) {
        return false;
    }
    for (uint8_t i = 0; i < BLE_NOTIF_MAX_COUNT; i++) {
        if (self->items[i].valid && self->items[i].id == id) {
            if (out_item != NULL) {
                *out_item = self->items[i];
            }
            return true;
        }
    }
    return false;
}

bool ble_notifications_get_at_index(const ble_notifications_t *self, uint8_t idx,
                                    ble_notification_item_t *out_item) {
    if (self == NULL || idx >= self->count) {
        return false;
    }
    /* 0 = newest (head_idx), 1 = head_idx - 1, etc. */
    int slot = (int)self->head_idx - (int)idx;
    if (slot < 0) {
        slot += (int)BLE_NOTIF_MAX_COUNT;
    }
    if (!self->items[slot].valid) {
        return false;
    }
    if (out_item != NULL) {
        *out_item = self->items[slot];
    }
    return true;
}

bool ble_notifications_dismiss(ble_notifications_t *self, uint8_t id) {
    if (self == NULL || id == 0 || self->count == 0) {
        return false;
    }
    for (uint8_t i = 0; i < BLE_NOTIF_MAX_COUNT; i++) {
        if (self->items[i].valid && self->items[i].id == id) {
            self->items[i].valid = false;
            self->count--;

            if (self->event_sink != NULL) {
                edge_event_t ev = {
                    .id = EDGE_EVT_WATCH_NOTIF_DISMISSED,
                    .source = EDGE_MOD_BLE_NOTIFICATIONS,
                    .arg0 = (uint32_t)id,
                    .arg1 = 0u,
                    .timestamp = 0u,
                };
                edge_event_sink_push_isr(self->event_sink, &ev);
            }
            return true;
        }
    }
    return false;
}

void ble_notifications_clear_all(ble_notifications_t *self) {
    if (self == NULL) {
        return;
    }
    for (uint8_t i = 0; i < BLE_NOTIF_MAX_COUNT; i++) {
        self->items[i].valid = false;
    }
    self->count = 0;
    self->head_idx = 0;
}

edge_status_t ble_notifications_accept_call(ble_notifications_t *self) {
    if (self == NULL || self->call_port.send_call_response == NULL) {
        return EDGE_EINVAL;
    }
    return self->call_port.send_call_response(self->call_port.self, BLE_NOTIF_CALL_ACCEPT);
}

edge_status_t ble_notifications_reject_call(ble_notifications_t *self) {
    if (self == NULL || self->call_port.send_call_response == NULL) {
        return EDGE_EINVAL;
    }
    return self->call_port.send_call_response(self->call_port.self, BLE_NOTIF_CALL_REJECT);
}

edge_status_t ble_notifications_mute_call(ble_notifications_t *self) {
    if (self == NULL || self->call_port.send_call_response == NULL) {
        return EDGE_EINVAL;
    }
    return self->call_port.send_call_response(self->call_port.self, BLE_NOTIF_CALL_MUTE);
}
