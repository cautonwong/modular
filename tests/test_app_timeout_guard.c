/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <cmocka.h>
/* clang-format on */

#include "edge/errors.h"
#include "edge/modules.h"
#include "timeout_guard/timeout_guard.h"

typedef struct mock_guard_motor {
    bool stopped;
    float brake_current;
} mock_guard_motor_t;

static edge_status_t mock_emergency_stop(void *self) {
    mock_guard_motor_t *m = (mock_guard_motor_t *)self;
    m->stopped = true;
    return EDGE_OK;
}

static edge_status_t mock_set_brake_current(void *self, float current) {
    mock_guard_motor_t *m = (mock_guard_motor_t *)self;
    m->brake_current = current;
    m->stopped = true;
    return EDGE_OK;
}

static void test_timeout_guard_triggers_after_silence(void **state) {
    (void)state;
    mock_guard_motor_t motor_ctx = {0};

    timeout_motor_port_t motor_port = {
        .emergency_stop = mock_emergency_stop,
        .set_brake_current = mock_set_brake_current,
        .self = &motor_ctx,
    };

    /* Caller-provided storage, as in the product. */
    static alignas(
        TIMEOUT_GUARD_STORAGE_ALIGN) unsigned char guard_storage[TIMEOUT_GUARD_STORAGE_SIZE];
    timeout_guard_t *guard = (timeout_guard_t *)guard_storage;
    memset(guard_storage, 0, sizeof(guard_storage));
    timeout_guard_construct(guard, EDGE_MOD_TIMEOUT_GUARD, 5, &motor_port, 500, 15.0f, 1);
    assert_int_equal(timeout_guard_init(guard), EDGE_OK);

    edge_module_t *mod = timeout_guard_module(guard);
    assert_non_null(mod);

    /* Feed at t=100ms */
    assert_int_equal(timeout_guard_feed(guard, 100), EDGE_OK);
    assert_false(timeout_guard_is_timed_out(guard));

    /* Poll at t=300ms (within 500ms timeout) */
    mod->next_due = 300;
    assert_int_equal(mod->poll(mod), EDGE_OK);
    assert_false(timeout_guard_is_timed_out(guard));
    assert_false(motor_ctx.stopped);

    /* Poll at t=700ms (600ms elapsed > 500ms timeout) */
    mod->next_due = 700;
    assert_int_equal(mod->poll(mod), EDGE_OK);
    assert_true(timeout_guard_is_timed_out(guard));
    assert_true(motor_ctx.stopped);
    assert_true(fabsf(motor_ctx.brake_current - 15.0f) < 1e-3f);
}

/*
 * The guard's own edges. `is_timed_out` reports TRUE for a missing guard, which is
 * the safe reading - a caller that lost its guard has lost its supervision - and
 * that is worth pinning rather than leaving to whoever reads the ternary next.
 */
static void test_timeout_guard_null_guards(void **state) {
    (void)state;
    assert_int_equal(timeout_guard_init(NULL), EDGE_EINVAL);
    assert_int_equal(timeout_guard_deinit(NULL), EDGE_EINVAL);
    assert_int_equal(timeout_guard_feed(NULL, 0u), EDGE_EINVAL);
    assert_true(timeout_guard_is_timed_out(NULL));
    assert_ptr_equal(timeout_guard_module(NULL), NULL);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_timeout_guard_triggers_after_silence),
        cmocka_unit_test(test_timeout_guard_null_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
