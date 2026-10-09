#include "metronome/metronome.h"

static uint32_t get_now_ms(const metronome_app_t *self) {
    if (self->clock != NULL && self->clock->get_tick_ms != NULL) {
        return self->clock->get_tick_ms(self->clock->self);
    }
    return 0u;
}

static edge_status_t metronome_poll(edge_module_t *module) {
    metronome_app_t *self = (metronome_app_t *)module->private_data;
    if (self == NULL || !self->is_running || self->bpm == 0) {
        return EDGE_OK;
    }

    uint32_t now = get_now_ms(self);
    uint32_t interval_ms = 60000u / (uint32_t)self->bpm;

    if (now - self->last_beat_tick_ms >= interval_ms) {
        self->last_beat_tick_ms = now;
        self->total_beats_played++;

        /* Accent pulse on first beat of bar, regular pulse otherwise */
        uint32_t pulse_dur =
            (self->current_beat == 0) ? METRONOME_ACCENT_PULSE_MS : METRONOME_BEAT_PULSE_MS;

        if (self->motor != NULL && self->motor->run_duration_ms != NULL) {
            (void)self->motor->run_duration_ms(self->motor->self, pulse_dur);
        }

        self->current_beat = (self->current_beat + 1u) % (self->bpb > 0 ? self->bpb : 1u);
    }

    return EDGE_OK;
}

static edge_status_t metronome_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t metronome_power_off(edge_module_t *module) {
    metronome_app_t *self = (metronome_app_t *)module->private_data;
    if (self != NULL) {
        self->is_running = false;
    }
    return EDGE_OK;
}

void metronome_construct(metronome_app_t *self, uint32_t module_id, uint32_t priority,
                         const metronome_motor_if_t *motor, const metronome_clock_if_t *clock) {
    if (self == NULL) {
        return;
    }
    *self = (__typeof__(*self)){0};
    self->module.module_id = module_id;
    self->module.priority = priority;
    self->module.period = 10u;
    self->module.budget = 1u;
    self->module.next_due = 0u;
    self->module.poll = metronome_poll;
    self->module.on_event = metronome_on_event;
    self->module.power_off = metronome_power_off;
    self->module.private_data = self;

    self->motor = motor;
    self->clock = clock;
    self->bpm = METRONOME_DEFAULT_BPM;
    self->bpb = METRONOME_DEFAULT_BPB;
    self->current_beat = 0;
    self->is_running = false;
}

edge_status_t metronome_init(metronome_app_t *self, const metronome_motor_if_t *motor,
                             const metronome_clock_if_t *clock) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    metronome_construct(self, 0x3800u, 50u, motor, clock);
    return EDGE_OK;
}

edge_status_t metronome_shutdown(metronome_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_running = false;
    return EDGE_OK;
}

void metronome_start(metronome_app_t *self) {
    if (self != NULL) {
        self->is_running = true;
        self->current_beat = 0;
        self->last_beat_tick_ms = get_now_ms(self);
    }
}

void metronome_stop(metronome_app_t *self) {
    if (self != NULL) {
        self->is_running = false;
    }
}

void metronome_toggle(metronome_app_t *self) {
    if (self != NULL) {
        if (self->is_running) {
            metronome_stop(self);
        } else {
            metronome_start(self);
        }
    }
}

edge_status_t metronome_set_bpm(metronome_app_t *self, uint16_t bpm) {
    if (self == NULL || bpm < METRONOME_MIN_BPM || bpm > METRONOME_MAX_BPM) {
        return EDGE_EINVAL;
    }
    self->bpm = bpm;
    return EDGE_OK;
}

uint16_t metronome_get_bpm(const metronome_app_t *self) {
    return self != NULL ? self->bpm : METRONOME_DEFAULT_BPM;
}

edge_status_t metronome_set_bpb(metronome_app_t *self, uint8_t bpb) {
    if (self == NULL || bpb < 1u || bpb > METRONOME_MAX_BPB) {
        return EDGE_EINVAL;
    }
    self->bpb = bpb;
    self->current_beat = 0;
    return EDGE_OK;
}

uint8_t metronome_get_bpb(const metronome_app_t *self) {
    return self != NULL ? self->bpb : METRONOME_DEFAULT_BPB;
}

void metronome_tap_tempo(metronome_app_t *self) {
    if (self == NULL) {
        return;
    }
    uint32_t now = get_now_ms(self);
    uint32_t delta = now - self->last_tap_tick_ms;
    self->last_tap_tick_ms = now;

    if (delta > 0 && delta < 3000u) {
        uint32_t calc_bpm = 60000u / delta;
        if (calc_bpm >= METRONOME_MIN_BPM && calc_bpm <= METRONOME_MAX_BPM) {
            self->bpm = (uint16_t)calc_bpm;
        }
    }
}
