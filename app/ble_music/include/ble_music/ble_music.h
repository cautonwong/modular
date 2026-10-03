#ifndef BLE_MUSIC_H
#define BLE_MUSIC_H

#include "ble_music/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_MUSIC_MAX_STRING_LEN 40u

typedef enum ble_music_command {
    BLE_MUSIC_CMD_PLAY = 0x00,
    BLE_MUSIC_CMD_PAUSE = 0x01,
    BLE_MUSIC_CMD_NEXT = 0x03,
    BLE_MUSIC_CMD_PREV = 0x04,
    BLE_MUSIC_CMD_VOLUP = 0x05,
    BLE_MUSIC_CMD_VOLDOWN = 0x06,
    BLE_MUSIC_CMD_OPEN = 0xE0
} ble_music_command_t;

typedef struct ble_music_info {
    char artist[BLE_MUSIC_MAX_STRING_LEN + 1u];
    char track[BLE_MUSIC_MAX_STRING_LEN + 1u];
    char album[BLE_MUSIC_MAX_STRING_LEN + 1u];
    bool playing;
    int32_t track_progress; /* seconds */
    int32_t track_length;   /* seconds */
    int32_t track_number;
    int32_t tracks_total;
    int32_t playback_speed; /* scaled x100 (e.g. 100 = 1.0x) */
    bool repeat;
    bool shuffle;
    uint64_t last_update_ms;
} ble_music_info_t;

typedef struct ble_music {
    edge_module_t module;
    ble_music_transport_port_t transport;
    edge_event_sink_t *event_sink;
    ble_music_info_t info;
} ble_music_t;

void ble_music_init(ble_music_t *self, const ble_music_transport_port_t *transport,
                    edge_event_sink_t *event_sink);

void ble_music_construct(ble_music_t *self, uint32_t module_id, uint32_t priority,
                         const ble_music_transport_port_t *transport,
                         edge_event_sink_t *event_sink);

edge_status_t ble_music_send_command(ble_music_t *self, ble_music_command_t cmd);

void ble_music_set_artist(ble_music_t *self, const char *artist, size_t len);
void ble_music_set_track(ble_music_t *self, const char *track, size_t len);
void ble_music_set_album(ble_music_t *self, const char *album, size_t len);
void ble_music_set_status(ble_music_t *self, bool playing);
void ble_music_set_position(ble_music_t *self, int32_t pos_sec);
void ble_music_set_total_length(ble_music_t *self, int32_t len_sec);
void ble_music_set_track_number(ble_music_t *self, int32_t num);
void ble_music_set_tracks_total(ble_music_t *self, int32_t total);
void ble_music_set_playback_speed(ble_music_t *self, int32_t speed_scaled_100);
void ble_music_set_repeat(ble_music_t *self, bool repeat);
void ble_music_set_shuffle(ble_music_t *self, bool shuffle);

bool ble_music_get_info(const ble_music_t *self, ble_music_info_t *out_info);
int32_t ble_music_get_progress(const ble_music_t *self);

const edge_module_t *ble_music_module(void);

#ifdef __cplusplus
}
#endif

#endif /* BLE_MUSIC_H */
