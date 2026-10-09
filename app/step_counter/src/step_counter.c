#include "step_counter/step_counter.h"

#include "edge/events.h"

static const int16_t s_sin_table_90[91] = {
    0,     571,   1143,  1714,  2285,  2855,  3425,  3993,  4560,  5125,  5689,  6252,  6812,
    7370,  7927,  8480,  9031,  9580,  10125, 10667, 11206, 11742, 12274, 12803, 13327, 13848,
    14364, 14875, 15382, 15884, 16383, 16876, 17363, 17846, 18323, 18794, 19259, 19719, 20172,
    20619, 21060, 21494, 21921, 22342, 22755, 23161, 23560, 23951, 24334, 24710, 25078, 25437,
    25789, 26132, 26466, 26792, 27109, 27417, 27716, 28005, 28286, 28556, 28818, 29069, 29311,
    29543, 29764, 29976, 30177, 30368, 30548, 30718, 30877, 31026, 31164, 31291, 31407, 31513,
    31607, 31691, 31763, 31825, 31876, 31915, 31944, 31962, 31968, 31963, 31948, 31920, 32767};

static inline int32_t i32_abs(int32_t v) {
    return v < 0 ? -v : v;
}

static inline int32_t clamp_val(int32_t val, int32_t min_v, int32_t max_v) {
    return val < min_v ? min_v : (val > max_v ? max_v : val);
}

int16_t step_counter_fast_asin(int16_t arg) {
    int32_t a = (arg < 0) ? -(int32_t)arg : (int32_t)arg;
    if (a > 32767) {
        a = 32767;
    }

    int16_t low = 0;
    int16_t high = 90;
    int16_t angle = 45;

    while (low <= high) {
        int16_t sin_angle = s_sin_table_90[angle];
        int16_t sin_prev = (angle > 0) ? s_sin_table_90[angle - 1] : 0;
        int16_t sin_next = (angle < 90) ? s_sin_table_90[angle + 1] : 32767;

        if (a >= sin_prev && a <= sin_next) {
            if (a <= (sin_prev + sin_angle) / 2 && angle > 0) {
                angle--;
            } else if (a > (sin_angle + sin_next) / 2 && angle < 90) {
                angle++;
            }
            break;
        }

        if (a < sin_angle) {
            high = (int16_t)(angle - 1);
        } else {
            low = (int16_t)(angle + 1);
        }
        angle = (int16_t)((low + high) / 2);
    }

    return arg < 0 ? (int16_t)(-angle) : angle;
}

static int16_t degrees_rolled(int16_t y, int16_t z, int16_t prev_y, int16_t prev_z) {
    int16_t prev_y_angle = step_counter_fast_asin((int16_t)clamp_val(prev_y * 32, -32767, 32767));
    int16_t y_angle = step_counter_fast_asin((int16_t)clamp_val(y * 32, -32767, 32767));

    if (z < 0 && prev_z < 0) {
        return (int16_t)(y_angle - prev_y_angle);
    }
    if (prev_z < 0) {
        if (y < 0) {
            return (int16_t)(-prev_y_angle - y_angle - 180);
        }
        return (int16_t)(-prev_y_angle - y_angle + 180);
    }
    if (z < 0) {
        if (y < 0) {
            return (int16_t)(prev_y_angle + y_angle + 180);
        }
        return (int16_t)(prev_y_angle + y_angle - 180);
    }
    return (int16_t)(prev_y_angle - y_angle);
}

static accel_stats_t compute_accel_stats(const step_counter_t *self) {
    accel_stats_t stats = {0};
    int64_t sum_x = 0, sum_y = 0, sum_z = 0;
    int64_t prev_sum_x = 0, prev_sum_y = 0, prev_sum_z = 0;

    for (uint8_t i = 0; i < ACCEL_STATS_HISTORY; i++) {
        uint8_t curr_idx = (self->hist_head + STEP_HISTORY_SIZE - i) % STEP_HISTORY_SIZE;
        uint8_t prev_idx =
            (self->hist_head + STEP_HISTORY_SIZE - 1 - i - ACCEL_STATS_HISTORY) % STEP_HISTORY_SIZE;

        sum_x += self->x_history[curr_idx];
        sum_y += self->y_history[curr_idx];
        sum_z += self->z_history[curr_idx];

        prev_sum_x += self->x_history[prev_idx];
        prev_sum_y += self->y_history[prev_idx];
        prev_sum_z += self->z_history[prev_idx];
    }

    stats.x_mean = (int32_t)(sum_x / ACCEL_STATS_HISTORY);
    stats.y_mean = (int32_t)(sum_y / ACCEL_STATS_HISTORY);
    stats.z_mean = (int32_t)(sum_z / ACCEL_STATS_HISTORY);
    stats.prev_x_mean = (int32_t)(prev_sum_x / ACCEL_STATS_HISTORY);
    stats.prev_y_mean = (int32_t)(prev_sum_y / ACCEL_STATS_HISTORY);
    stats.prev_z_mean = (int32_t)(prev_sum_z / ACCEL_STATS_HISTORY);

    int64_t var_x = 0, var_y = 0, var_z = 0;
    for (uint8_t i = 0; i < ACCEL_STATS_HISTORY; i++) {
        uint8_t curr_idx = (self->hist_head + STEP_HISTORY_SIZE - i) % STEP_HISTORY_SIZE;
        int64_t dx = (int64_t)self->x_history[curr_idx] - stats.x_mean;
        int64_t dy = (int64_t)self->y_history[curr_idx] - stats.y_mean;
        int64_t dz = (int64_t)self->z_history[curr_idx] - stats.z_mean;

        var_x += dx * dx;
        var_y += dy * dy;
        var_z += dz * dz;
    }

    stats.x_variance = (int32_t)(var_x / ACCEL_STATS_HISTORY);
    stats.y_variance = (int32_t)(var_y / ACCEL_STATS_HISTORY);
    stats.z_variance = (int32_t)(var_z / ACCEL_STATS_HISTORY);

    return stats;
}

static edge_status_t step_counter_poll(edge_module_t *module) {
    step_counter_t *self = (step_counter_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->poll_count++;

    if (!self->is_running || self->sensor == NULL) {
        return EDGE_OK;
    }

    int16_t x = 0, y = 0, z = 0;
    uint32_t raw_steps = 0;

    if (self->sensor->read_accel != NULL) {
        self->sensor->read_accel(self->sensor->self, &x, &y, &z);
    }
    if (self->sensor->read_steps != NULL) {
        self->sensor->read_steps(self->sensor->self, &raw_steps);
    }

    step_counter_update_motion(self, x, y, z, raw_steps, self->poll_count * 10u);
    return EDGE_OK;
}

static edge_status_t step_counter_on_event(edge_module_t *module, const edge_event_t *event) {
    step_counter_t *self = (step_counter_t *)edge_module_data(module);
    if (self == NULL || event == NULL) {
        return EDGE_EINVAL;
    }
    if (event->id == EDGE_EVT_WATCH_NEW_DAY) {
        step_counter_advance_day(self);
    }
    return EDGE_OK;
}

static edge_status_t step_counter_power_off(edge_module_t *module) {
    step_counter_t *self = (step_counter_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return step_counter_shutdown(self);
}

void step_counter_construct(step_counter_t *self, uint32_t module_id, uint32_t priority,
                            const imu_sensor_if_t *sensor) {
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
        .poll = step_counter_poll,
        .on_event = step_counter_on_event,
        .power_off = step_counter_power_off,
        .private_data = self,
    };
    self->sensor = sensor;
}

edge_status_t step_counter_init(step_counter_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->today_steps = 0;
    self->yesterday_steps = 0;
    self->trip_steps = 0;
    self->last_raw_steps = 0;
    self->hist_head = 0;
    self->accumulated_speed = 0;
    self->last_tick = 0;
    for (size_t i = 0; i < STEP_HISTORY_SIZE; ++i) {
        self->x_history[i] = 0;
    }
    for (size_t i = 0; i < STEP_HISTORY_SIZE; ++i) {
        self->y_history[i] = 0;
    }
    for (size_t i = 0; i < STEP_HISTORY_SIZE; ++i) {
        self->z_history[i] = 0;
    }
    self->stats = (__typeof__(self->stats)){0};
    self->is_running = true;
    self->poll_count = 0;

    return EDGE_OK;
}

edge_status_t step_counter_shutdown(step_counter_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_running = false;
    return EDGE_OK;
}

void step_counter_update_motion(step_counter_t *self, int16_t x, int16_t y, int16_t z,
                                uint32_t raw_steps, uint32_t tick) {
    if (self == NULL) {
        return;
    }

    if (raw_steps > self->last_raw_steps) {
        uint32_t delta = raw_steps - self->last_raw_steps;
        self->trip_steps += delta;
    }
    self->last_raw_steps = raw_steps;
    self->today_steps = raw_steps;

    self->hist_head = (self->hist_head + 1) % STEP_HISTORY_SIZE;
    self->x_history[self->hist_head] = x;
    self->y_history[self->hist_head] = y;
    self->z_history[self->hist_head] = z;

    uint32_t dt = (tick >= self->last_tick) ? (tick - self->last_tick) : 10u;
    if (dt == 0) {
        dt = 1;
    }
    self->last_tick = tick;

    uint8_t oldest_idx = (self->hist_head + 1) % STEP_HISTORY_SIZE;
    int32_t dz = self->z_history[self->hist_head] - self->z_history[oldest_idx];
    int32_t dy = (self->y_history[self->hist_head] - self->y_history[oldest_idx]) / 2;
    int32_t dx = (self->x_history[self->hist_head] - self->x_history[oldest_idx]) / 4;

    int32_t speed = i32_abs(dz + dy + dx) * 100 / (int32_t)dt;
    self->accumulated_speed = speed / 5 + (self->accumulated_speed * 4) / 5;

    self->stats = compute_accel_stats(self);
}

bool step_counter_should_raise_wake(const step_counter_t *self) {
    if (self == NULL) {
        return false;
    }

    const int32_t variance_thresh = 56 * 56;
    const int16_t x_thresh = 384;
    const int16_t y_thresh = -64;
    const int16_t roll_degrees_thresh = -45;

    if (i32_abs((int32_t)self->stats.x_mean) > x_thresh) {
        return false;
    }

    if (self->stats.y_variance > variance_thresh ||
        (self->stats.y_mean < -724 && self->stats.z_variance > variance_thresh) ||
        self->stats.y_mean > y_thresh) {
        return false;
    }

    return degrees_rolled((int16_t)self->stats.y_mean, (int16_t)self->stats.z_mean,
                          (int16_t)self->stats.prev_y_mean,
                          (int16_t)self->stats.prev_z_mean) < roll_degrees_thresh;
}

bool step_counter_should_lower_sleep(const step_counter_t *self) {
    if (self == NULL) {
        return false;
    }

    if ((self->stats.x_mean > 887 &&
         degrees_rolled((int16_t)self->stats.x_mean, (int16_t)self->stats.z_mean,
                        (int16_t)self->stats.prev_x_mean, (int16_t)self->stats.prev_z_mean) > 30) ||
        (self->stats.x_mean < -887 &&
         degrees_rolled((int16_t)self->stats.x_mean, (int16_t)self->stats.z_mean,
                        (int16_t)self->stats.prev_x_mean,
                        (int16_t)self->stats.prev_z_mean) < -30)) {
        return true;
    }

    if (self->stats.y_mean < 724 ||
        degrees_rolled((int16_t)self->stats.y_mean, (int16_t)self->stats.z_mean,
                       (int16_t)self->stats.prev_y_mean, (int16_t)self->stats.prev_z_mean) < 30) {
        return false;
    }

    for (uint8_t i = ACCEL_STATS_HISTORY + 1; i < STEP_HISTORY_SIZE; i++) {
        uint8_t idx = (self->hist_head + STEP_HISTORY_SIZE - i) % STEP_HISTORY_SIZE;
        if (self->y_history[idx] < 265) {
            return false;
        }
    }

    return true;
}

void step_counter_advance_day(step_counter_t *self) {
    if (self == NULL) {
        return;
    }
    self->yesterday_steps = self->today_steps;
    self->today_steps = 0;
}

void step_counter_reset_trip(step_counter_t *self) {
    if (self == NULL) {
        return;
    }
    self->trip_steps = 0;
}

uint32_t step_counter_get_steps(const step_counter_t *self) {
    return self != NULL ? self->today_steps : 0;
}

uint32_t step_counter_get_trip_steps(const step_counter_t *self) {
    return self != NULL ? self->trip_steps : 0;
}
