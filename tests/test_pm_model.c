#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/pm.h"

static void test_pm_wake_lock_behavior(void **state) {
    (void)state;
    edge_pm_t pm;
    edge_pm_init(&pm);

    assert_false(edge_pm_has_wake_locks(&pm));
    assert_int_equal(edge_pm_evaluate_state(&pm, true, 0), EDGE_PM_STATE_IDLE);

    /* Acquire BLE and Flash wake locks */
    edge_pm_wake_lock_acquire(&pm, EDGE_PM_LOCK_BLE_TX | EDGE_PM_LOCK_FLASH_WRITE);
    assert_true(edge_pm_has_wake_locks(&pm));
    /* While wake lock held, state must stay RUN */
    assert_int_equal(edge_pm_evaluate_state(&pm, true, 0), EDGE_PM_STATE_RUN);

    /* Release BLE, Flash still held */
    edge_pm_wake_lock_release(&pm, EDGE_PM_LOCK_BLE_TX);
    assert_true(edge_pm_has_wake_locks(&pm));
    assert_int_equal(edge_pm_evaluate_state(&pm, true, 0), EDGE_PM_STATE_RUN);

    /* Release Flash */
    edge_pm_wake_lock_release(&pm, EDGE_PM_LOCK_FLASH_WRITE);
    assert_false(edge_pm_has_wake_locks(&pm));
    assert_int_equal(edge_pm_evaluate_state(&pm, true, 0), EDGE_PM_STATE_IDLE);
}

static void test_pm_deadline_evaluation(void **state) {
    (void)state;
    edge_pm_t pm;
    edge_pm_init(&pm);

    /* Set next deadline to 5000 ticks in future */
    pm.next_deadline_ticks = 5000;
    edge_pm_state_t st = edge_pm_evaluate_state(&pm, true, 1000);
    assert_int_equal(st, EDGE_PM_STATE_LIGHT_SLEEP);

    /* If queue is not empty, must stay RUN */
    st = edge_pm_evaluate_state(&pm, false, 1000);
    assert_int_equal(st, EDGE_PM_STATE_RUN);
}

static void test_pm_wake_source_management(void **state) {
    (void)state;
    edge_pm_t pm;
    edge_pm_init(&pm);

    edge_pm_set_wake_source(&pm, EDGE_PM_WAKE_GPIO | EDGE_PM_WAKE_RTC, true);
    assert_true((pm.active_wake_sources & EDGE_PM_WAKE_GPIO) != 0);
    assert_true((pm.active_wake_sources & EDGE_PM_WAKE_RTC) != 0);

    edge_pm_set_wake_source(&pm, EDGE_PM_WAKE_GPIO, false);
    assert_true((pm.active_wake_sources & EDGE_PM_WAKE_GPIO) == 0);
    assert_true((pm.active_wake_sources & EDGE_PM_WAKE_RTC) != 0);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pm_wake_lock_behavior),
        cmocka_unit_test(test_pm_deadline_evaluation),
        cmocka_unit_test(test_pm_wake_source_management),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
