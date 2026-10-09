#include "watch_time/watch_time.h"

#include "edge/events.h"

#define RTC_MAX_COUNTER 0x00FFFFFFu /* 24-bit RTC counter */

static bool is_leap_year(uint16_t year) {
    return (year % 4u == 0u && year % 100u != 0u) || (year % 400u == 0u);
}

static uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return (month >= 1 && month <= 12) ? days[month] : 30;
}

static uint8_t compute_day_of_week(uint16_t year, uint8_t month, uint8_t day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year - (month < 3);
    int dow = (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
    /* dow: 0 = Sun, 1 = Mon, ..., 6 = Sat. Map to 1 = Mon .. 7 = Sun */
    return (dow == 0) ? 7u : (uint8_t)dow;
}

static edge_status_t watch_time_poll(edge_module_t *module) {
    watch_time_t *self = (watch_time_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->poll_count++;

    if (!self->is_running || self->clock == NULL || self->clock->get_counter == NULL) {
        return EDGE_OK;
    }

    uint32_t counter = 0;
    if (self->clock->get_counter(self->clock->self, &counter) != EDGE_OK) {
        return EDGE_OK;
    }

    uint32_t freq = (self->clock->get_tick_frequency != NULL)
                        ? self->clock->get_tick_frequency(self->clock->self)
                        : 1000u;
    if (freq == 0) {
        freq = 1000u;
    }

    uint32_t delta_ticks = 0;
    if (counter < self->prev_rtc_counter) {
        delta_ticks = (RTC_MAX_COUNTER - self->prev_rtc_counter) + counter + 1u;
    } else {
        delta_ticks = counter - self->prev_rtc_counter;
    }

    delta_ticks += self->rtc_remainder;
    uint32_t elapsed_seconds = delta_ticks / freq;
    self->rtc_remainder = delta_ticks % freq;
    self->prev_rtc_counter = counter;

    if (elapsed_seconds > 0) {
        watch_time_advance_seconds(self, elapsed_seconds);
    }

    return EDGE_OK;
}

static edge_status_t watch_time_on_event(edge_module_t *module, const edge_event_t *event) {
    (void)module;
    (void)event;
    return EDGE_OK;
}

static edge_status_t watch_time_power_off(edge_module_t *module) {
    watch_time_t *self = (watch_time_t *)edge_module_data(module);
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    return watch_time_shutdown(self);
}

void watch_time_construct(watch_time_t *self, uint32_t module_id, uint32_t priority,
                          const rtc_clock_if_t *clock) {
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
        .poll = watch_time_poll,
        .on_event = watch_time_on_event,
        .power_off = watch_time_power_off,
        .private_data = self,
    };
    self->clock = clock;
    self->current_time = (watch_datetime_t){
        .year = 2026,
        .month = 1,
        .day = 1,
        .hour = 0,
        .minute = 0,
        .second = 0,
        .day_of_week = 4, /* Thursday */
    };
}

edge_status_t watch_time_init(watch_time_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->prev_rtc_counter = 0;
    self->rtc_remainder = 0;
    self->uptime_seconds = 0;
    self->last_notified_hour = 0xFFu;
    self->last_notified_minute = 0xFFu;
    self->last_notified_day = self->current_time.day;
    self->is_running = true;
    self->poll_count = 0;

    if (self->clock != NULL && self->clock->get_counter != NULL) {
        self->clock->get_counter(self->clock->self, &self->prev_rtc_counter);
    }
    return EDGE_OK;
}

edge_status_t watch_time_shutdown(watch_time_t *self) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    self->is_running = false;
    return EDGE_OK;
}

edge_status_t watch_time_set(watch_time_t *self, uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint8_t second) {
    if (self == NULL || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 ||
        minute > 59 || second > 59) {
        return EDGE_EINVAL;
    }

    self->current_time.year = year;
    self->current_time.month = month;
    self->current_time.day = day;
    self->current_time.hour = hour;
    self->current_time.minute = minute;
    self->current_time.second = second;
    self->current_time.day_of_week = compute_day_of_week(year, month, day);

    self->last_notified_day = day;
    self->last_notified_hour = hour;
    self->last_notified_minute = minute;

    return EDGE_OK;
}

edge_status_t watch_time_advance_seconds(watch_time_t *self, uint32_t seconds) {
    if (self == NULL || seconds == 0) {
        return EDGE_OK;
    }

    self->uptime_seconds += seconds;

    for (uint32_t s = 0; s < seconds; s++) {
        self->current_time.second++;
        if (self->current_time.second >= 60) {
            self->current_time.second = 0;
            self->current_time.minute++;

            if (self->current_time.minute >= 60) {
                self->current_time.minute = 0;
                self->current_time.hour++;

                if (self->current_time.hour >= 24) {
                    self->current_time.hour = 0;
                    self->current_time.day++;
                    self->current_time.day_of_week = (self->current_time.day_of_week % 7u) + 1u;

                    if (self->current_time.day >
                        days_in_month(self->current_time.year, self->current_time.month)) {
                        self->current_time.day = 1;
                        self->current_time.month++;

                        if (self->current_time.month > 12) {
                            self->current_time.month = 1;
                            self->current_time.year++;
                        }
                    }
                }
            }
        }
    }

    return EDGE_OK;
}

watch_datetime_t watch_time_get(const watch_time_t *self) {
    if (self == NULL) {
        return (watch_datetime_t){0};
    }
    return self->current_time;
}

static inline void format_2digits(char *dst, uint8_t val) {
    dst[0] = (char)('0' + (val / 10u));
    dst[1] = (char)('0' + (val % 10u));
}

edge_status_t watch_time_format(const watch_time_t *self, bool format_12h, char *out_buf,
                                size_t buf_len) {
    if (self == NULL || out_buf == NULL || buf_len < 9) {
        return EDGE_EINVAL;
    }

    if (format_12h) {
        uint8_t h12 = self->current_time.hour;
        const char *ampm = "AM";
        if (h12 >= 12u) {
            ampm = "PM";
            if (h12 > 12u) {
                h12 -= 12u;
            }
        } else if (h12 == 0u) {
            h12 = 12u;
        }

        size_t pos = 0;
        if (h12 >= 10u) {
            out_buf[pos++] = (char)('0' + (h12 / 10u));
        }
        out_buf[pos++] = (char)('0' + (h12 % 10u));
        out_buf[pos++] = ':';
        format_2digits(&out_buf[pos], self->current_time.minute);
        pos += 2u;
        out_buf[pos++] = ' ';
        out_buf[pos++] = ampm[0];
        out_buf[pos++] = ampm[1];
        out_buf[pos] = '\0';
    } else {
        format_2digits(&out_buf[0], self->current_time.hour);
        out_buf[2] = ':';
        format_2digits(&out_buf[3], self->current_time.minute);
        out_buf[5] = ':';
        format_2digits(&out_buf[6], self->current_time.second);
        out_buf[8] = '\0';
    }
    return EDGE_OK;
}
