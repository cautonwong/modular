#ifndef APP_METRONOME_H
#define APP_METRONOME_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "metronome/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METRONOME_MIN_BPM 40u
#define METRONOME_MAX_BPM 220u
#define METRONOME_DEFAULT_BPM 120u
#define METRONOME_DEFAULT_BPB 4u
#define METRONOME_MAX_BPB 9u

#define METRONOME_ACCENT_PULSE_MS 90u
#define METRONOME_BEAT_PULSE_MS 30u

typedef struct metronome_app {
    edge_module_t module;
    const metronome_motor_if_t *motor;
    const metronome_clock_if_t *clock;

    uint16_t bpm;
    uint8_t bpb;
    uint8_t current_beat;
    bool is_running;

    uint32_t last_beat_tick_ms;
    uint32_t last_tap_tick_ms;
    uint32_t total_beats_played;
} metronome_app_t;

void metronome_construct(metronome_app_t *self, uint32_t module_id, uint32_t priority,
                         const metronome_motor_if_t *motor, const metronome_clock_if_t *clock);

edge_status_t metronome_init(metronome_app_t *self, const metronome_motor_if_t *motor,
                             const metronome_clock_if_t *clock);

edge_status_t metronome_shutdown(metronome_app_t *self);

void metronome_start(metronome_app_t *self);
void metronome_stop(metronome_app_t *self);
void metronome_toggle(metronome_app_t *self);

edge_status_t metronome_set_bpm(metronome_app_t *self, uint16_t bpm);
uint16_t metronome_get_bpm(const metronome_app_t *self);

edge_status_t metronome_set_bpb(metronome_app_t *self, uint8_t bpb);
uint8_t metronome_get_bpb(const metronome_app_t *self);

void metronome_tap_tempo(metronome_app_t *self);

#ifdef __cplusplus
}
#endif

#endif /* APP_METRONOME_H */
