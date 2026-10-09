#include "stopwatch/stopwatch.h"

static edge_status_t stopwatch_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

static edge_status_t stopwatch_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t stopwatch_power_off(edge_module_t *module) {
    stopwatch_app_t *self = (stopwatch_app_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return stopwatch_shutdown(self);
}

void stopwatch_construct(stopwatch_app_t *self, uint32_t module_id, uint32_t priority,
                         const stopwatch_clock_if_t *clock) {
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
        .poll = stopwatch_poll,
        .on_event = stopwatch_on_event,
        .power_off = stopwatch_power_off,
        .private_data = self,
    };

    self->clock = clock;
    stopwatch_clear(self);
}

edge_status_t stopwatch_init(stopwatch_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    stopwatch_clear(self);
    return EDGE_OK;
}

edge_status_t stopwatch_shutdown(stopwatch_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    stopwatch_clear(self);
    return EDGE_OK;
}

static inline uint32_t get_now_ms(const stopwatch_app_t *self) {
    if (self->clock != NULL && self->clock->get_tick_ms != NULL) {
        return self->clock->get_tick_ms(self->clock->self);
    }
    return 0u;
}

void stopwatch_start(stopwatch_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->state = STOPWATCH_RUNNING;
    self->start_time_ms = get_now_ms(self);
}

void stopwatch_pause(stopwatch_app_t *self) {
    if (self == NULL || self->state != STOPWATCH_RUNNING) {
        return;
    }
    self->time_elapsed_previously_ms = stopwatch_get_elapsed_ms(self);
    self->state = STOPWATCH_PAUSED;
}

void stopwatch_clear(stopwatch_app_t *self) {
    if (self == NULL) {
        return;
    }
    self->state = STOPWATCH_CLEARED;
    self->start_time_ms = 0u;
    self->time_elapsed_previously_ms = 0u;
    self->max_lap_number = 0u;
    self->lap_count = 0u;
    for (size_t i = 0; i < STOPWATCH_HIST_SIZE; ++i) {
        self->history[i] = (stopwatch_lap_t){0};
    }
}

uint32_t stopwatch_get_elapsed_ms(stopwatch_app_t *self) {
    if (self == NULL) {
        return 0u;
    }
    if (self->state != STOPWATCH_RUNNING) {
        return self->time_elapsed_previously_ms;
    }

    uint32_t now = get_now_ms(self);
    uint32_t delta = (now >= self->start_time_ms) ? (now - self->start_time_ms)
                                                  : ((UINT32_MAX - self->start_time_ms) + now + 1u);

    return (self->time_elapsed_previously_ms + delta) % STOPWATCH_MAX_HOURS_MS;
}

edge_status_t stopwatch_add_lap(stopwatch_app_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }

    uint32_t lap_end = stopwatch_get_elapsed_ms(self);

    for (int i = (int)STOPWATCH_HIST_SIZE - 1; i > 0; i--) {
        self->history[i] = self->history[i - 1];
    }

    self->max_lap_number = (self->max_lap_number + 1u) % STOPWATCH_LAP_BOUNDARY;
    if (self->max_lap_number == 0u) {
        self->max_lap_number = 1u;
    }

    self->history[0].number = self->max_lap_number;
    self->history[0].time_since_start_ms = lap_end;

    if (self->lap_count < STOPWATCH_HIST_SIZE) {
        self->lap_count++;
    }

    return EDGE_OK;
}

uint16_t stopwatch_get_max_lap_number(const stopwatch_app_t *self) {
    return self ? self->max_lap_number : 0u;
}

edge_status_t stopwatch_get_lap(const stopwatch_app_t *self, uint8_t index,
                                stopwatch_lap_t *out_lap) {
    if (self == NULL || out_lap == NULL || index >= STOPWATCH_HIST_SIZE ||
        self->history[index].number == 0u) {
        return EDGE_ENOENT;
    }
    *out_lap = self->history[index];
    return EDGE_OK;
}

uint32_t stopwatch_get_lap_duration_ms(const stopwatch_app_t *self, uint8_t index) {
    if (self == NULL || index >= STOPWATCH_HIST_SIZE || self->history[index].number == 0u) {
        return 0u;
    }
    if (index + 1u < STOPWATCH_HIST_SIZE && self->history[index + 1u].number != 0u) {
        if (self->history[index].time_since_start_ms >=
            self->history[index + 1u].time_since_start_ms) {
            return self->history[index].time_since_start_ms -
                   self->history[index + 1u].time_since_start_ms;
        }
    }
    return self->history[index].time_since_start_ms;
}

bool stopwatch_is_running(const stopwatch_app_t *self) {
    return self && (self->state == STOPWATCH_RUNNING);
}

bool stopwatch_is_cleared(const stopwatch_app_t *self) {
    return self && (self->state == STOPWATCH_CLEARED);
}

bool stopwatch_is_paused(const stopwatch_app_t *self) {
    return self && (self->state == STOPWATCH_PAUSED);
}
