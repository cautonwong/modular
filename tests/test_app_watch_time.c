#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/events.h"
#include "edge/modules.h"
#include "watch_time/watch_time.h"

typedef struct mock_clock {
    uint32_t counter;
    uint32_t freq;
} mock_clock_t;

// cppcheck-suppress constParameterPointer ; signature fixed by consumer port
// cppcheck-suppress constParameterCallback ; signature fixed by consumer port
static edge_status_t mock_get_counter(void *self, uint32_t *counter) {
    const mock_clock_t *mock = (const mock_clock_t *)self;
    *counter = mock->counter;
    return EDGE_OK;
}

// cppcheck-suppress constParameterPointer ; signature fixed by consumer port
// cppcheck-suppress constParameterCallback ; signature fixed by consumer port
static uint32_t mock_get_freq(void *self) {
    const mock_clock_t *mock = (const mock_clock_t *)self;
    return mock->freq;
}

static void test_watch_time_calendar_and_format(void **state) {
    (void)state;
    watch_time_t wt;
    watch_time_construct(&wt, EDGE_MOD_WATCH_TIME, 100u, NULL);
    assert_int_equal(watch_time_init(&wt), EDGE_OK);

    // Set time to 2024 (leap year) Feb 28 23:59:58
    assert_int_equal(watch_time_set(&wt, 2024, 2, 28, 23, 59, 58), EDGE_OK);

    char buf[32];
    assert_int_equal(watch_time_format(&wt, false, buf, sizeof(buf)), EDGE_OK);
    assert_string_equal(buf, "23:59:58");

    assert_int_equal(watch_time_format(&wt, true, buf, sizeof(buf)), EDGE_OK);
    assert_string_equal(buf, "11:59 PM");

    // Advance 2 seconds -> should roll into Feb 29 (leap day!)
    assert_int_equal(watch_time_advance_seconds(&wt, 2), EDGE_OK);
    watch_datetime_t dt = watch_time_get(&wt);
    assert_int_equal(dt.year, 2024);
    assert_int_equal(dt.month, 2);
    assert_int_equal(dt.day, 29);
    assert_int_equal(dt.hour, 0);
    assert_int_equal(dt.minute, 0);
    assert_int_equal(dt.second, 0);

    // Advance 86400 seconds (1 full day) -> should roll into March 1
    assert_int_equal(watch_time_advance_seconds(&wt, 86400), EDGE_OK);
    dt = watch_time_get(&wt);
    assert_int_equal(dt.year, 2024);
    assert_int_equal(dt.month, 3);
    assert_int_equal(dt.day, 1);
}

static void test_watch_time_rtc_poll_and_wrap(void **state) {
    (void)state;
    mock_clock_t mock = {.counter = 0x00FFFF00u, .freq = 1000u};
    rtc_clock_if_t clock_if = {
        .get_counter = mock_get_counter,
        .get_tick_frequency = mock_get_freq,
        .self = &mock,
    };

    watch_time_t wt;
    watch_time_construct(&wt, EDGE_MOD_WATCH_TIME, 100u, &clock_if);
    assert_int_equal(watch_time_init(&wt), EDGE_OK);
    assert_int_equal(watch_time_set(&wt, 2026, 10, 1, 12, 0, 0), EDGE_OK);

    // Advance RTC counter across 24-bit wrap boundary
    // 0x00FFFF00 -> 0x000003E8 (+1000 ticks) -> 1 second elapsed
    mock.counter = 1000u;

    assert_int_equal(wt.module.poll(&wt.module), EDGE_OK);
    watch_datetime_t dt = watch_time_get(&wt);
    assert_int_equal(dt.second, 1);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_watch_time_calendar_and_format),
        cmocka_unit_test(test_watch_time_rtc_poll_and_wrap),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
