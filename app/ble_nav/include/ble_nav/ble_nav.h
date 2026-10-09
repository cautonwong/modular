#ifndef BLE_NAV_H
#define BLE_NAV_H

#include "ble_nav/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_NAV_FLAG_MAX_LEN 16u
#define BLE_NAV_NARRATIVE_MAX_LEN 64u
#define BLE_NAV_MANDIST_MAX_LEN 16u

typedef struct ble_nav_info {
    char flag[BLE_NAV_FLAG_MAX_LEN + 1u];
    char narrative[BLE_NAV_NARRATIVE_MAX_LEN + 1u];
    char man_dist[BLE_NAV_MANDIST_MAX_LEN + 1u];
    uint8_t progress; /* 0..100 percentage */
    bool valid;
} ble_nav_info_t;

typedef struct ble_nav {
    edge_module_t module;
    edge_event_sink_t *event_sink;
    ble_nav_info_t info;
} ble_nav_t;

void ble_nav_init(ble_nav_t *self, edge_event_sink_t *event_sink);

void ble_nav_construct(ble_nav_t *self, uint32_t module_id, uint32_t priority,
                       edge_event_sink_t *event_sink);

void ble_nav_set_flag(ble_nav_t *self, const char *flag, size_t len);
void ble_nav_set_narrative(ble_nav_t *self, const char *narrative, size_t len);
void ble_nav_set_man_dist(ble_nav_t *self, const char *dist, size_t len);
void ble_nav_set_progress(ble_nav_t *self, uint8_t progress);

bool ble_nav_get_info(const ble_nav_t *self, ble_nav_info_t *out_info);
void ble_nav_clear(ble_nav_t *self);

const edge_module_t *ble_nav_module(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_NAV_H */
