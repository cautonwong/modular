#ifndef APP_BLE_PASSKEY_H
#define APP_BLE_PASSKEY_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ble_passkey_state {
    BLE_PASSKEY_IDLE = 0,
    BLE_PASSKEY_DISPLAYING = 1,
    BLE_PASSKEY_CONFIRMED = 2,
    BLE_PASSKEY_TIMEOUT = 3,
} ble_passkey_state_t;

typedef struct ble_passkey_app {
    edge_module_t module;
    uint32_t passkey;
    ble_passkey_state_t state;
    uint32_t display_start_ms;
    uint32_t timeout_ms;
} ble_passkey_app_t;

void ble_passkey_construct(ble_passkey_app_t *self, uint32_t module_id, uint32_t priority);
edge_status_t ble_passkey_init(ble_passkey_app_t *self);
edge_status_t ble_passkey_shutdown(ble_passkey_app_t *self);

edge_status_t ble_passkey_show(ble_passkey_app_t *self, uint32_t key, uint32_t timeout_ms);
void ble_passkey_confirm(ble_passkey_app_t *self);
void ble_passkey_dismiss(ble_passkey_app_t *self);

uint32_t ble_passkey_get_key(const ble_passkey_app_t *self);
ble_passkey_state_t ble_passkey_get_state(const ble_passkey_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_BLE_PASSKEY_H */
