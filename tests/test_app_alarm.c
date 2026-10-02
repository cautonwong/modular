#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "alarm/alarm.h"
#include "edge/events.h"
#include "edge/modules.h"

static alarm_settings_t g_saved_alarm;
static uint32_t g_save_count = 0;
static uint32_t g_load_count = 0;
static bool g_alert_started = false;
static bool g_alert_stopped = false;

static edge_status_t mock_alarm_load(void *self, alarm_settings_t *out_settings) {
    (void)self;
    g_load_count++;
    *out_settings = g_saved_alarm;
    return EDGE_OK;
}

static edge_status_t mock_alarm_save(void *self, const alarm_settings_t *settings) {
    (void)self;
    g_save_count++;
    g_saved_alarm = *settings;
    return EDGE_OK;
}

static edge_status_t mock_alert_start(void *self) {
    (void)self;
    g_alert_started = true;
    g_alert_stopped = false;
    return EDGE_OK;
}

static edge_status_t mock_alert_stop(void *self) {
    (void)self;
    g_alert_stopped = true;
    g_alert_started = false;
    return EDGE_OK;
}

static void test_alarm_lifecycle_and_trigger(void **state) {
    (void)state;

    g_save_count = 0;
    g_load_count = 0;
    g_alert_started = false;
    g_alert_stopped = false;
    g_saved_alarm = (alarm_settings_t){
        .version = ALARM_FORMAT_VERSION,
        .hours = 7,
        .minutes = 30,
        .recurrence = ALARM_RECUR_DAILY,
        .is_enabled = true,
    };

    const alarm_storage_if_t storage_if = {
        .load = mock_alarm_load,
        .save = mock_alarm_save,
        .self = NULL,
    };
    const alarm_alert_if_t alert_if = {
        .start_alert = mock_alert_start,
        .stop_alert = mock_alert_stop,
        .self = NULL,
    };

    alarm_app_t app;
    alarm_construct(&app, EDGE_MOD_ALARM, 20u, &storage_if, &alert_if);
    assert_int_equal(alarm_init(&app), EDGE_OK);
    assert_int_equal(g_load_count, 1);
    assert_int_equal(app.settings.hours, 7);
    assert_int_equal(app.settings.minutes, 30);
    assert_true(app.settings.is_enabled);

    /* Time: 07:29 -> does not trigger */
    assert_false(alarm_check_trigger(&app, 7, 29, 1, 1));
    assert_false(g_alert_started);

    /* Time: 07:30 -> triggers */
    assert_true(alarm_check_trigger(&app, 7, 30, 1, 1));
    assert_true(g_alert_started);
    assert_true(app.is_alerting);

    /* Same minute again -> does not re-trigger */
    assert_false(alarm_check_trigger(&app, 7, 30, 1, 1));

    /* User stops alerting */
    alarm_stop_alerting(&app);
    assert_false(app.is_alerting);
    assert_true(g_alert_stopped);
    /* Daily recurrence -> remains enabled */
    assert_true(app.settings.is_enabled);
}

static void test_alarm_snooze_and_weekdays(void **state) {
    (void)state;

    g_alert_started = false;
    g_alert_stopped = false;

    const alarm_alert_if_t alert_if = {
        .start_alert = mock_alert_start,
        .stop_alert = mock_alert_stop,
        .self = NULL,
    };

    alarm_app_t app;
    alarm_construct(&app, EDGE_MOD_ALARM, 20u, NULL, &alert_if);
    assert_int_equal(alarm_set_time(&app, 8, 0), EDGE_OK);
    assert_int_equal(alarm_set_recurrence(&app, ALARM_RECUR_WEEKDAYS), EDGE_OK);
    assert_int_equal(alarm_enable(&app, true), EDGE_OK);

    /* Saturday (DOW = 6): 08:00 -> does not trigger */
    assert_false(alarm_check_trigger(&app, 8, 0, 6, 10));
    assert_false(g_alert_started);

    /* Monday (DOW = 1): 08:00 -> triggers */
    assert_true(alarm_check_trigger(&app, 8, 0, 1, 12));
    assert_true(g_alert_started);

    /* Snooze for 10 minutes -> targets 08:10 */
    alarm_snooze(&app, 10);
    assert_false(app.is_alerting);
    assert_true(app.is_snoozing);
    assert_int_equal(app.snooze_target_hour, 8);
    assert_int_equal(app.snooze_target_minute, 10);

    /* 08:05 -> does not trigger */
    assert_false(alarm_check_trigger(&app, 8, 5, 1, 12));

    /* 08:10 -> snooze alert triggers! */
    assert_true(alarm_check_trigger(&app, 8, 10, 1, 12));
    assert_true(app.is_alerting);
    assert_false(app.is_snoozing);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_alarm_lifecycle_and_trigger),
        cmocka_unit_test(test_alarm_snooze_and_weekdays),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
