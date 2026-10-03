#ifndef ZMK_ENDPOINTS_H
#define ZMK_ENDPOINTS_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZMK_MAX_BLE_PROFILES 5u

enum zmk_endpoint_type {
    ZMK_ENDPOINT_USB = 0,
    ZMK_ENDPOINT_BLE = 1,
};

typedef struct zmk_endpoint_event_sink_if {
    void *self;
    edge_status_t (*post_endpoint_changed)(void *self, uint8_t endpoint, uint8_t active_profile);
} zmk_endpoint_event_sink_if_t;

typedef struct zmk_endpoints_app {
    edge_module_t module;
    const zmk_endpoint_event_sink_if_t *sink;

    uint8_t current_endpoint;   /* ZMK_ENDPOINT_USB or ZMK_ENDPOINT_BLE */
    uint8_t active_ble_profile; /* 0 .. ZMK_MAX_BLE_PROFILES - 1 */
    bool usb_connected;
    bool ble_connected[ZMK_MAX_BLE_PROFILES];
} zmk_endpoints_app_t;

void zmk_endpoints_construct(zmk_endpoints_app_t *self, uint32_t module_id, uint32_t priority,
                             const zmk_endpoint_event_sink_if_t *sink);

edge_status_t zmk_endpoints_init(zmk_endpoints_app_t *self);
edge_status_t zmk_endpoints_shutdown(zmk_endpoints_app_t *self);

edge_status_t zmk_endpoints_select_endpoint(zmk_endpoints_app_t *self, uint8_t endpoint);
edge_status_t zmk_endpoints_toggle_endpoint(zmk_endpoints_app_t *self);
uint8_t zmk_endpoints_get_current_endpoint(const zmk_endpoints_app_t *self);

edge_status_t zmk_endpoints_select_profile(zmk_endpoints_app_t *self, uint8_t profile);
edge_status_t zmk_endpoints_next_profile(zmk_endpoints_app_t *self);
edge_status_t zmk_endpoints_prev_profile(zmk_endpoints_app_t *self);
uint8_t zmk_endpoints_get_active_profile(const zmk_endpoints_app_t *self);

void zmk_endpoints_set_usb_status(zmk_endpoints_app_t *self, bool connected);
void zmk_endpoints_set_ble_status(zmk_endpoints_app_t *self, uint8_t profile, bool connected);

#ifdef __cplusplus
}
#endif

#endif /* ZMK_ENDPOINTS_H */
