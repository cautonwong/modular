#ifndef GUARD_BLE_MUSIC_H
#define GUARD_BLE_MUSIC_H

#include "edge/event.h"

typedef struct ble_music_guard ble_music_guard_t;
typedef struct ble_music_port_guard ble_music_port_guard_t;

/* Deliberately absent from the old hard-coded producer list: the gate must catch
 * this because the parameter type says a sink is required, not because of a name. */
edge_status_t ble_music_init(ble_music_guard_t *self, ble_music_port_guard_t *port,
                             edge_event_sink_t *sink);

#endif
