#ifndef APP_WATCH_TIME_H
#define APP_WATCH_TIME_H

#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"
#include "watch_time/ports.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct watch_datetime {
    uint16_t year;
    uint8_t month;       /* 1..12 */
    uint8_t day;         /* 1..31 */
    uint8_t hour;        /* 0..23 */
    uint8_t minute;      /* 0..59 */
    uint8_t second;      /* 0..59 */
    uint8_t day_of_week; /* 1 = Mon, 7 = Sun */
} watch_datetime_t;

typedef struct watch_time {
    edge_module_t module;
    const rtc_clock_if_t *clock;

    watch_datetime_t current_time;
    uint32_t uptime_seconds;

    /* RTC sync & counter tracking */
    uint32_t prev_rtc_counter;
    uint32_t rtc_remainder;
    bool is_running;
    uint32_t poll_count;

    /* Periodic notification latch */
    uint8_t last_notified_hour;
    uint8_t last_notified_minute;
    uint8_t last_notified_day;
} watch_time_t;

void watch_time_construct(watch_time_t *self, uint32_t module_id, uint32_t priority,
                          const rtc_clock_if_t *clock);

edge_status_t watch_time_init(watch_time_t *self);
edge_status_t watch_time_shutdown(watch_time_t *self);

edge_status_t watch_time_set(watch_time_t *self, uint16_t year, uint8_t month, uint8_t day,
                             uint8_t hour, uint8_t minute, uint8_t second);

edge_status_t watch_time_advance_seconds(watch_time_t *self, uint32_t seconds);

watch_datetime_t watch_time_get(const watch_time_t *self);

edge_status_t watch_time_format(const watch_time_t *self, bool format_12h, char *out_buf,
                                size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* APP_WATCH_TIME_H */
