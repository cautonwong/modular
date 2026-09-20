#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "example/board.h"
#include "example/sys.h"
#include "pal_host/host.h"
#include "pal_os/idle.h"

static bool g_pending;
static uint32_t g_depth_at_check;

static bool fake_pending(void *ctx) {
    (void)ctx;
    g_depth_at_check = pal_host_critical_depth();
    return g_pending;
}

/* D71: the re-check runs with interrupts disabled, and the wait is skipped when
 * work arrived between the decision to idle and the sleep. */
static void test_atomic_idle_sequence(void **state) {
    (void)state;
    const edge_pal_port_t pal = pal_host_port();
    const uint32_t before = pal_host_idle_waits();

    g_pending = false;
    edge_os_idle_wait(&pal, fake_pending, NULL);
    assert_int_equal(pal_host_idle_waits() - before, 1u);
    assert_int_equal(g_depth_at_check, 1u); /* checked inside the critical section */
    assert_int_equal(pal_host_critical_depth(), 0u);

    g_pending = true;
    edge_os_idle_wait(&pal, fake_pending, NULL);
    assert_int_equal(pal_host_idle_waits() - before, 1u); /* skipped */
    assert_int_equal(pal_host_critical_depth(), 0u);

    edge_os_idle_wait(&pal, NULL, NULL); /* no predicate always waits */
    assert_int_equal(pal_host_idle_waits() - before, 2u);
    edge_os_idle_wait(NULL, NULL, NULL); /* null PAL is safe */
}

static void test_board_hardware_actions(void **state) {
    (void)state;
    board_example_enter_low_power();
    board_example_feed_watchdog();
    board_example_feed_watchdog();
    board_example_system_reset();
    assert_int_equal(board_example_low_power_entries(), 1u);
    assert_int_equal(board_example_watchdog_feeds(), 2u);
    assert_int_equal(board_example_reset_count(), 1u);
}

typedef struct fake_app {
    edge_module_t module;
} fake_app_t;

static uint64_t g_now;

static uint64_t clock_now(void *self) {
    (void)self;
    return g_now;
}

static edge_status_t fake_poll(edge_module_t *module) {
    (void)module;
    return EDGE_OK;
}

/* D9: the health signal the watchdog policy reads. */
static void test_health_and_pending(void **state) {
    (void)state;
    fake_app_t app = {0};
    edge_module_t *apps[1];
    edge_sys_t sys;
    const edge_clock_port_t clock = {.monotonic_ticks = clock_now, .wall_time = NULL, .self = NULL};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];

    app.module = (edge_module_t){.module_id = 1u, .priority = 1u, .period = 1u, .poll = fake_poll};
    apps[0] = &app.module;

    assert_false(edge_sys_healthy(NULL));
    assert_false(edge_sys_pending(NULL));
    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_set_clock(&sys, &clock), EDGE_OK);
    g_now = 10u;
    assert_int_equal(edge_sys_start(&sys), EDGE_OK); /* next_due = 11 */

    assert_true(edge_sys_healthy(&sys));
    assert_false(edge_sys_pending(&sys));
    g_now = 11u;
    assert_true(edge_sys_pending(&sys));

    app.module.failed = 1u;
    assert_false(edge_sys_healthy(&sys));
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_atomic_idle_sequence),
        cmocka_unit_test(test_board_hardware_actions),
        cmocka_unit_test(test_health_and_pending),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
