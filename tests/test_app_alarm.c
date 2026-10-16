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
    alarm_settings_t settings;
    assert_int_equal(alarm_get_settings(&app, &settings), EDGE_OK);
    assert_int_equal(settings.hours, 7);
    assert_int_equal(settings.minutes, 30);
    assert_true(settings.is_enabled);

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
    assert_int_equal(alarm_get_settings(&app, &settings), EDGE_OK);
    assert_true(settings.is_enabled);
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

static edge_status_t mock_alarm_load_corrupted(void *self, alarm_settings_t *out_settings) {
    (void)self;
    *out_settings = (alarm_settings_t){
        .version = ALARM_FORMAT_VERSION,
        .hours = 25,                          /* Invalid: > 23 */
        .minutes = 61,                        /* Invalid: > 59 */
        .recurrence = (alarm_recurrence_t)99, /* Invalid */
        .is_enabled = true,
    };
    return EDGE_OK;
}

static void test_alarm_invariants(void **state) {
    (void)state;

    /* 1. Validation function tests */
    assert_false(alarm_validate_settings(NULL));

    alarm_settings_t valid = {
        .version = ALARM_FORMAT_VERSION,
        .hours = 12,
        .minutes = 30,
        .recurrence = ALARM_RECUR_DAILY,
        .is_enabled = true,
    };
    assert_true(alarm_validate_settings(&valid));

    alarm_settings_t invalid_ver = valid;
    invalid_ver.version = 99;
    assert_false(alarm_validate_settings(&invalid_ver));

    alarm_settings_t invalid_hours = valid;
    invalid_hours.hours = 24;
    assert_false(alarm_validate_settings(&invalid_hours));

    alarm_settings_t invalid_mins = valid;
    invalid_mins.minutes = 60;
    assert_false(alarm_validate_settings(&invalid_mins));

    alarm_settings_t invalid_recur = valid;
    invalid_recur.recurrence = (alarm_recurrence_t)5;
    assert_false(alarm_validate_settings(&invalid_recur));

    /* 2. Setter validation tests */
    alarm_app_t app;
    alarm_construct(&app, EDGE_MOD_ALARM, 20u, NULL, NULL);

    assert_int_equal(alarm_set_time(NULL, 10, 10), EDGE_EINVAL);
    assert_int_equal(alarm_set_time(&app, 24, 0), EDGE_EINVAL);
    assert_int_equal(alarm_set_time(&app, 0, 60), EDGE_EINVAL);
    assert_int_equal(alarm_set_time(&app, 23, 59), EDGE_OK);

    assert_int_equal(alarm_set_recurrence(NULL, ALARM_RECUR_NONE), EDGE_EINVAL);
    assert_int_equal(alarm_set_recurrence(&app, (alarm_recurrence_t)10), EDGE_EINVAL);
    assert_int_equal(alarm_set_recurrence(&app, ALARM_RECUR_DAILY), EDGE_OK);

    assert_int_equal(alarm_enable(NULL, true), EDGE_EINVAL);
    assert_int_equal(alarm_enable(&app, true), EDGE_OK);

    alarm_settings_t got;
    assert_int_equal(alarm_get_settings(NULL, &got), EDGE_EINVAL);
    assert_int_equal(alarm_get_settings(&app, NULL), EDGE_EINVAL);
    assert_int_equal(alarm_get_settings(&app, &got), EDGE_OK);
    assert_int_equal(got.hours, 23);
    assert_int_equal(got.minutes, 59);
    assert_int_equal(got.recurrence, ALARM_RECUR_DAILY);
    assert_true(got.is_enabled);

    /* 3. Rejection of corrupted storage data on init */
    const alarm_storage_if_t corrupt_storage = {
        .load = mock_alarm_load_corrupted,
        .save = NULL,
        .self = NULL,
    };
    alarm_app_t corrupt_app;
    alarm_construct(&corrupt_app, EDGE_MOD_ALARM, 20u, &corrupt_storage, NULL);
    assert_int_equal(alarm_init(&corrupt_app), EDGE_OK);

    /* Must retain default settings (07:00, disabled) rather than corrupted values */
    assert_int_equal(alarm_get_settings(&corrupt_app, &got), EDGE_OK);
    assert_int_equal(got.hours, 7);
    assert_int_equal(got.minutes, 0);
    assert_false(got.is_enabled);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_alarm_lifecycle_and_trigger),
        cmocka_unit_test(test_alarm_snooze_and_weekdays),
        cmocka_unit_test(test_alarm_invariants),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
