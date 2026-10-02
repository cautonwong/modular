#ifndef BLE_NOTIFICATIONS_H
#define BLE_NOTIFICATIONS_H

#include "ble_notifications/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_NOTIF_MAX_COUNT 5u
#define BLE_NOTIF_MESSAGE_MAX_LEN 100u

typedef enum ble_notif_category {
    BLE_NOTIF_CAT_SIMPLE_ALERT = 0,
    BLE_NOTIF_CAT_EMAIL = 1,
    BLE_NOTIF_CAT_NEWS = 2,
    BLE_NOTIF_CAT_INCOMING_CALL = 3,
    BLE_NOTIF_CAT_MISSED_CALL = 4,
    BLE_NOTIF_CAT_SMS = 5,
    BLE_NOTIF_CAT_VOICE_MAIL = 6,
    BLE_NOTIF_CAT_SCHEDULE = 7,
    BLE_NOTIF_CAT_HIGH_PRIORITY = 8,
    BLE_NOTIF_CAT_INSTANT_MESSAGE = 9,
    BLE_NOTIF_CAT_UNKNOWN = 255
} ble_notif_category_t;

typedef enum ble_notif_call_response {
    BLE_NOTIF_CALL_REJECT = 0x00,
    BLE_NOTIF_CALL_ACCEPT = 0x01,
    BLE_NOTIF_CALL_MUTE = 0x02
} ble_notif_call_response_t;

typedef struct ble_notification_item {
    uint8_t id;
    uint8_t category;
    char message[BLE_NOTIF_MESSAGE_MAX_LEN + 1u];
    uint8_t message_len;
    bool valid;
} ble_notification_item_t;

typedef struct ble_notifications {
    edge_module_t module;
    ble_notifications_call_port_t call_port;
    edge_event_sink_t *event_sink;
    ble_notification_item_t items[BLE_NOTIF_MAX_COUNT];
    uint8_t head_idx; /* points to newest item */
    uint8_t count;
    uint8_t next_id;
} ble_notifications_t;

void ble_notifications_init(ble_notifications_t *self,
                            const ble_notifications_call_port_t *call_port,
                            edge_event_sink_t *event_sink);

void ble_notifications_construct(ble_notifications_t *self, uint32_t module_id, uint32_t priority,
                                 const ble_notifications_call_port_t *call_port,
                                 edge_event_sink_t *event_sink);

edge_status_t ble_notifications_process_ans_packet(ble_notifications_t *self, const uint8_t *data,
                                                   size_t len);

uint8_t ble_notifications_push(ble_notifications_t *self, uint8_t category, const char *msg,
                               size_t len);

uint8_t ble_notifications_count(const ble_notifications_t *self);
bool ble_notifications_get_by_id(const ble_notifications_t *self, uint8_t id,
                                 ble_notification_item_t *out_item);
bool ble_notifications_get_at_index(const ble_notifications_t *self, uint8_t idx,
                                    ble_notification_item_t *out_item);

bool ble_notifications_dismiss(ble_notifications_t *self, uint8_t id);
void ble_notifications_clear_all(ble_notifications_t *self);

edge_status_t ble_notifications_accept_call(ble_notifications_t *self);
edge_status_t ble_notifications_reject_call(ble_notifications_t *self);
edge_status_t ble_notifications_mute_call(ble_notifications_t *self);

const edge_module_t *ble_notifications_module(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_NOTIFICATIONS_H */
