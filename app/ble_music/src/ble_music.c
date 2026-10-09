#include <stddef.h>
#include <stdint.h>
static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
}
#include "ble_music/ble_music.h"
#include "edge/events.h"
#include "edge/modules.h"

static void safe_strncpy(char *dst, const char *src, size_t max_len) {
    if (dst == NULL || max_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i < max_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static edge_status_t music_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

void ble_music_init(ble_music_t *self, const ble_music_transport_port_t *transport,
                    edge_event_sink_t *event_sink) {
    ble_music_construct(self, EDGE_MOD_BLE_MUSIC, 2u, transport, event_sink);
}

void ble_music_construct(ble_music_t *self, uint32_t module_id, uint32_t priority,
                         const ble_music_transport_port_t *transport,
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
        .poll = music_poll,
        .on_event = NULL,
        .power_off = NULL,
        .private_data = self,
    };
    if (transport != NULL) {
        self->transport = *transport;
    }
    self->event_sink = event_sink;
    safe_strncpy(self->info.artist, "Not Playing", BLE_MUSIC_MAX_STRING_LEN);
    self->info.playback_speed = 100; /* 1.0x */
}

edge_status_t ble_music_send_command(ble_music_t *self, ble_music_command_t cmd) {
    if (self == NULL || self->transport.send_event == NULL) {
        return EDGE_EINVAL;
    }
    return self->transport.send_event(self->transport.self, (uint8_t)cmd);
}

static void emit_updated_event(ble_music_t *self) {
    if (self != NULL && self->event_sink != NULL) {
        edge_event_t ev = {
            .id = EDGE_EVT_WATCH_MUSIC_UPDATED,
            .source = EDGE_MOD_BLE_MUSIC,
            .arg0 = self->info.playing ? 1u : 0u,
            .arg1 = 0u,
            .timestamp = 0u,
        };
        edge_event_sink_push_isr(self->event_sink, &ev);
    }
}

void ble_music_set_artist(ble_music_t *self, const char *artist, size_t len) {
    if (self == NULL) {
        return;
    }
    size_t copy_len = (len < BLE_MUSIC_MAX_STRING_LEN) ? len : BLE_MUSIC_MAX_STRING_LEN;
    copy_bytes(self->info.artist, artist, copy_len);
    self->info.artist[copy_len] = '\0';
    emit_updated_event(self);
}

void ble_music_set_track(ble_music_t *self, const char *track, size_t len) {
    if (self == NULL) {
        return;
    }
    size_t copy_len = (len < BLE_MUSIC_MAX_STRING_LEN) ? len : BLE_MUSIC_MAX_STRING_LEN;
    copy_bytes(self->info.track, track, copy_len);
    self->info.track[copy_len] = '\0';
    emit_updated_event(self);
}

void ble_music_set_album(ble_music_t *self, const char *album, size_t len) {
    if (self == NULL) {
        return;
    }
    size_t copy_len = (len < BLE_MUSIC_MAX_STRING_LEN) ? len : BLE_MUSIC_MAX_STRING_LEN;
    copy_bytes(self->info.album, album, copy_len);
    self->info.album[copy_len] = '\0';
    emit_updated_event(self);
}

void ble_music_set_status(ble_music_t *self, bool playing) {
    if (self == NULL) {
        return;
    }
    uint64_t now_ms = 0;
    if (self->transport.get_tick_ms != NULL) {
        now_ms = self->transport.get_tick_ms(self->transport.self);
    }
    if (self->info.playing && !playing) {
        /* Paused: freeze elapsed progress */
        uint64_t elapsed_ms = now_ms - self->info.last_update_ms;
        int32_t delta_sec =
            (int32_t)((elapsed_ms * (uint64_t)self->info.playback_speed) / 100000ULL);
        self->info.track_progress += delta_sec;
    }
    self->info.playing = playing;
    self->info.last_update_ms = now_ms;
    emit_updated_event(self);
}

void ble_music_set_position(ble_music_t *self, int32_t pos_sec) {
    if (self == NULL) {
        return;
    }
    self->info.track_progress = pos_sec;
    if (self->transport.get_tick_ms != NULL) {
        self->info.last_update_ms = self->transport.get_tick_ms(self->transport.self);
    }
    emit_updated_event(self);
}

void ble_music_set_total_length(ble_music_t *self, int32_t len_sec) {
    if (self == NULL) {
        return;
    }
    self->info.track_length = len_sec;
    emit_updated_event(self);
}

void ble_music_set_track_number(ble_music_t *self, int32_t num) {
    if (self == NULL) {
        return;
    }
    self->info.track_number = num;
}

void ble_music_set_tracks_total(ble_music_t *self, int32_t total) {
    if (self == NULL) {
        return;
    }
    self->info.tracks_total = total;
}

void ble_music_set_playback_speed(ble_music_t *self, int32_t speed_scaled_100) {
    if (self == NULL) {
        return;
    }
    if (speed_scaled_100 > 0) {
        self->info.playback_speed = speed_scaled_100;
    }
}

void ble_music_set_repeat(ble_music_t *self, bool repeat) {
    if (self == NULL) {
        return;
    }
    self->info.repeat = repeat;
}

void ble_music_set_shuffle(ble_music_t *self, bool shuffle) {
    if (self == NULL) {
        return;
    }
    self->info.shuffle = shuffle;
}

bool ble_music_get_info(const ble_music_t *self, ble_music_info_t *out_info) {
    if (self == NULL || out_info == NULL) {
        return false;
    }
    *out_info = self->info;
    return true;
}

int32_t ble_music_get_progress(const ble_music_t *self) {
    if (self == NULL) {
        return 0;
    }
    int32_t progress = self->info.track_progress;
    if (self->info.playing && self->transport.get_tick_ms != NULL) {
        uint64_t now_ms = self->transport.get_tick_ms(self->transport.self);
        uint64_t elapsed_ms = now_ms - self->info.last_update_ms;
        int32_t delta_sec =
            (int32_t)((elapsed_ms * (uint64_t)self->info.playback_speed) / 100000ULL);
        progress += delta_sec;
    }
    if (self->info.track_length > 0 && progress > self->info.track_length) {
        progress = self->info.track_length;
    }
    return progress;
}
