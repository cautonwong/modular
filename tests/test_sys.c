#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "bldc/sys.h"
#include "edge/event.h"
#include "edge/events.h"
#include "example/sys.h"
#include "meter/sys.h"

typedef struct fake_app {
    edge_module_t module;
    int poll_count;
    int event_count;
    int off_count;
    int suspend_count;
    int resume_count;
    edge_status_t poll_rc;
    edge_status_t event_rc;
    edge_status_t off_rc;
    edge_status_t suspend_rc;
    edge_status_t resume_rc;
    edge_sys_t *publish_sys;
    uint32_t publish_id;
    /* When set, this app unsubscribes itself the first time on_event runs (#176). */
    edge_sys_t *unsubscribe_sys;
    uint32_t unsubscribe_id;
    const edge_module_t *unsubscribe_app;
    int unsubscribe_count;
} fake_app_t;

static uint64_t g_now;
static uint64_t g_poll_advance;
static int g_idle_calls;
static edge_sys_t *g_shutdown_sys; /* runner-shutdown trigger, see test_step_and_run_shutdown */
static uint32_t g_off_order[4];    /* power_off call order, see the reverse-order test */
static uint32_t g_off_len;

static int g_shutdown_after_polls;

/*
 * Reset the module's state **before** each test rather than at the end of one: a
 * cmocka assertion longjmps out of the test, so an end-of-test reset is skipped
 * exactly when something has already gone wrong, and the next test inherits it
 * (#171).
 */
static int reset_state(void **state) {
    (void)state;
    g_now = 0u;
    g_poll_advance = 0u;
    g_idle_calls = 0;
    g_shutdown_sys = NULL;
    g_shutdown_after_polls = 0;
    g_off_len = 0u;
    return 0;
}

static void fake_idle(void *ctx) {
    (void)ctx;
    ++g_idle_calls;
}

static uint64_t fake_clock(void *self) {
    (void)self;
    return g_now;
}

static fake_app_t *as_fake(edge_module_t *m) {
    return (fake_app_t *)m->private_data;
}

static edge_status_t fake_poll(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->poll_count;
    g_now += g_poll_advance;
    if (g_shutdown_sys != NULL && g_shutdown_after_polls > 0 && --g_shutdown_after_polls == 0)
        (void)edge_sys_power_off(g_shutdown_sys);
    return a->poll_rc;
}

static edge_status_t fake_event(edge_module_t *m, const edge_event_t *e) {
    fake_app_t *a = as_fake(m);
    ++a->event_count;
    if (a->unsubscribe_sys != NULL) {
        (void)edge_sys_unsubscribe(a->unsubscribe_sys, a->unsubscribe_id, a->unsubscribe_app);
        ++a->unsubscribe_count;
        a->unsubscribe_sys = NULL; /* once is enough */
    }
    if (a->publish_sys != NULL) {
        const edge_event_t out = {.id = a->publish_id, .arg0 = e->id};
        (void)edge_sys_publish(a->publish_sys, &out);
    }
    return a->event_rc;
}

static edge_status_t fake_suspend(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->suspend_count;
    return a->suspend_rc;
}

static edge_status_t fake_resume(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->resume_count;
    return a->resume_rc;
}

static edge_status_t fake_off(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->off_count;
    if (g_off_len < (uint32_t)(sizeof(g_off_order) / sizeof(g_off_order[0])))
        g_off_order[g_off_len++] = m->module_id;
    return a->off_rc;
}

static void make_app(fake_app_t *a, uint32_t id, uint32_t priority) {
    *a = (fake_app_t){0};
    a->module = (edge_module_t){
        .module_id = id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .poll = fake_poll,
        .on_event = fake_event,
        .power_off = fake_off,
        .suspend = fake_suspend,
        .resume = fake_resume,
        .private_data = a,
    };
}

/* D51: sys does not call module init/deinit, so ordering is observed on the
 * scheduler's own module array (priority, then ascending module_id). */
static void test_start_sorts_by_priority_then_id(void **state) {
    (void)state;
    fake_app_t a, b, c;
    make_app(&a, 30u, 10u);
    make_app(&b, 20u, 10u);
    make_app(&c, 10u, 20u);
    edge_module_t *apps[] = {&a.module, &b.module, &c.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 3u), EDGE_OK);
    assert_int_equal(sys.apps[0]->module_id, 20u);
    assert_int_equal(sys.apps[1]->module_id, 30u);
    assert_int_equal(sys.apps[2]->module_id, 10u);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(sys.apps[0]->running, 1u);
    assert_int_equal(sys.apps[0]->next_due, 2u);
}

static void test_validation(void **state) {
    (void)state;
    fake_app_t a, b;
    make_app(&a, 7u, 1u);
    make_app(&b, 7u, 2u);
    edge_module_t *dup[] = {&a.module, &b.module};
    edge_sys_t sys;
    assert_int_equal(edge_sys_init(&sys, dup, 2u), EDGE_EBUSY);
    make_app(&b, 0u, 1u);
    edge_module_t *zero[] = {&b.module};
    assert_int_equal(edge_sys_init(&sys, zero, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_init(NULL, zero, 1u), EDGE_EINVAL);
}

static void test_required_and_lifecycle(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    const uint32_t ids[] = {1u};

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_required(&sys, ids, 1u), EDGE_OK);
    assert_int_equal(edge_sys_validate_required(&sys), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_set_required(&sys, ids, 1u), EDGE_ESTATE);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_OK);
    assert_int_equal(a.off_count, 1);
}

static void test_event_routing_and_bound(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[8];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[4];
    edge_sys_t sys;
    edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 8u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_EINVAL);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 4u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_EBUSY);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.event_count, 1);
    assert_int_equal(a.poll_count, 1);
}

static void test_periodic_scheduler(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.module.period = 3u;
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    edge_clock_port_t clock = {.monotonic_ticks = fake_clock, .wall_time = NULL};

    g_now = 100u;
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_clock(&sys, &clock), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 0);
    g_now = 102u;
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 0);
    g_now = 103u;
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 1);
    g_now = 105u;
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 1);
    g_now = 106u;
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 2);
}

static void test_event_budget_and_stats(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[8];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[8];
    edge_sys_t sys;
    edge_sys_stats_t stats;
    edge_event_t queued = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 8u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 8u), EDGE_OK);
    assert_int_equal(edge_sys_set_event_budget(&sys, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    for (int i = 0; i < 5; ++i)
        assert_int_equal(edge_event_push_isr(&queue, &queued), EDGE_OK);
    assert_int_equal(edge_sys_dispatch_events(&sys), EDGE_OK);
    assert_int_equal(a.event_count, 2);
    assert_int_equal(edge_event_count(&queue), 3u);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.events, 2u);
    assert_int_equal(stats.drops, 0u);
    assert_int_equal(edge_sys_stats_get(&sys, NULL), EDGE_EINVAL);
}

static void test_fault_isolation(void **state) {
    (void)state;
    fake_app_t bad, good;
    make_app(&bad, 1u, 1u);
    make_app(&good, 2u, 2u);
    bad.poll_rc = EDGE_EIO;
    edge_module_t *apps[] = {&bad.module, &good.module};
    edge_sys_t sys;
    edge_sys_stats_t stats;

    assert_int_equal(edge_sys_init(&sys, apps, 2u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_EIO);
    assert_int_equal(bad.module.failed, 1u);
    assert_int_equal(good.poll_count, 1);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(bad.poll_count, 1);
    assert_int_equal(good.poll_count, 2);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.isolated, 1u);
    assert_int_equal(stats.errors, 1u);
}

/*
 * #165: D82 says shutdown runs in reverse construction order, so a module can still
 * use what it was handed. The existing test used one app, which cannot observe an
 * order at all.
 */
static void test_power_off_runs_in_reverse_order(void **state) {
    (void)state;
    fake_app_t first;
    fake_app_t second;
    fake_app_t third;
    make_app(&first, 1u, 1u);
    make_app(&second, 2u, 1u);
    make_app(&third, 3u, 1u);
    edge_module_t *apps[] = {&first.module, &second.module, &third.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 3u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);

    assert_int_equal(g_off_len, 3u);
    assert_int_equal(g_off_order[0], 3u);
    assert_int_equal(g_off_order[1], 2u);
    assert_int_equal(g_off_order[2], 1u);
}

static void test_power_off_failure(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.off_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_EIO);
    assert_int_equal(a.module.failed, 1u);
}

static void test_wrapper_and_guards(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[2];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[1];
    edge_sys_t sys;

    assert_int_equal(edge_event_queue_init(&queue, storage, 2u), EDGE_OK);
    assert_int_equal(sys_example_init(&sys, apps, 1u, &queue, subs, 1u), EDGE_OK);

    /* sys_bldc is a thin composition-root callee like sys_example; it gets the same
     * treatment so it is not the one family with no test at all. */
    assert_int_equal(sys_bldc_init(&sys, apps, 1u, &queue, subs, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 1u), EDGE_ESTATE);
    assert_int_equal(edge_sys_start(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_ESTATE);
}

static void test_argument_and_state_guards(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_clock_port_t clock = {.monotonic_ticks = fake_clock, .wall_time = NULL};
    edge_sys_t sys;
    edge_sys_t fresh;
    const uint32_t ids[] = {1u};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, NULL, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_init(&sys, apps, 0u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(NULL, &queue, subs, 2u), EDGE_EINVAL);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, NULL, 2u), EDGE_EINVAL);
    assert_int_equal(edge_sys_set_clock(NULL, &clock), EDGE_EINVAL);
    assert_int_equal(edge_sys_set_clock(&sys, &clock), EDGE_OK);
    assert_int_equal(edge_sys_set_event_budget(NULL, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_set_event_budget(&sys, 0u), EDGE_EINVAL);
    assert_int_equal(edge_sys_set_event_budget(&sys, 2u), EDGE_OK);
    assert_int_equal(edge_sys_set_required(NULL, ids, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_set_required(&sys, NULL, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_validate_required(NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_EINVAL);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, 0u, &a.module), EDGE_EINVAL);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_TX_DONE, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_DLT645_RX, &a.module), EDGE_ENOSPC);
    assert_int_equal(edge_sys_dispatch_events(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_start(NULL), EDGE_ESTATE);
    assert_int_equal(edge_sys_set_clock(&sys, &clock), EDGE_ESTATE);
    assert_int_equal(edge_sys_set_event_budget(&sys, 2u), EDGE_ESTATE);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_ESTATE);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_DLT645_RX, &a.module), EDGE_ESTATE);
    assert_int_equal(edge_sys_dispatch_events(NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_init(&fresh, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_run_once(NULL), EDGE_ESTATE);
    assert_int_equal(edge_sys_run_once(&fresh), EDGE_ESTATE);
    assert_int_equal(edge_sys_dispatch_events(&fresh), EDGE_EINVAL);
    assert_int_equal(edge_sys_power_off(&fresh), EDGE_ESTATE);
}

static void test_required_missing(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    const uint32_t missing[] = {99u};

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_required(&sys, missing, 1u), EDGE_OK);
    assert_int_equal(edge_sys_validate_required(&sys), EDGE_EDEPEND);
    assert_int_equal(edge_sys_start(&sys), EDGE_EDEPEND);
    assert_int_equal(sys.state, EDGE_SYS_CONSTRUCTED);
}

static void test_dispatch_event_failure_and_pop_error(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.event_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;
    edge_sys_stats_t stats;
    edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_EIO);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.errors, 1u);
    assert_int_equal(stats.isolated, 1u);

    queue.items = NULL;
    assert_int_equal(edge_sys_dispatch_events(&sys), EDGE_EINVAL);
}

static void test_execution_budget(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.module.period = 1u;
    a.module.budget = 1u;
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    edge_sys_stats_t stats;
    edge_clock_port_t clock = {.monotonic_ticks = fake_clock, .wall_time = NULL};

    g_now = 10u;
    g_poll_advance = 5u;
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_clock(&sys, &clock), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    g_now = 11u;
    assert_int_equal(edge_sys_run_once(&sys), EDGE_EOVERFLOW);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.budget_hits, 1u);
    assert_int_equal(stats.polls, 1u);
    g_poll_advance = 0u;
}

static void test_deinit_stops_scheduler_only(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_OK);
    /* D51: module teardown is the composition root's job, so deinit only stops
     * scheduling and clears the scheduler-owned flags. */
    assert_int_equal(a.module.running, 0u);
    assert_int_equal(a.module.suspended, 0u);
}

static void test_meter_wrapper(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;
    const uint32_t required[] = {1u};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(sys_meter_init(&sys, apps, 1u, required, 1u, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_validate_required(&sys), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);

    assert_int_equal(sys_meter_init(&sys, NULL, 1u, required, 1u, &queue, subs, 2u), EDGE_EINVAL);
    assert_int_equal(sys_meter_init(&sys, apps, 1u, required, 1u, NULL, subs, 2u), EDGE_EINVAL);
    assert_int_equal(sys_meter_init(&sys, apps, 1u, NULL, 1u, &queue, subs, 2u), EDGE_EINVAL);
}

static void test_publish_deferred_queue(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_event_t pending[4];
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;
    edge_sys_stats_t stats;
    edge_event_t ev = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_event_budget(&sys, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_bind_pending_queue(&sys, pending, 4u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_publish(&sys, &ev), EDGE_ESTATE);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_publish(&sys, &ev), EDGE_OK);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.pending_high_water, 1u);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.event_count, 1);
}

static void test_publish_overflow_and_binding(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t pending[2];
    edge_sys_t sys;
    edge_sys_stats_t stats;
    edge_event_t ev = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_publish(&sys, &ev), EDGE_ENOTSUP);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_pending_queue(&sys, pending, 2u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_publish(&sys, &ev), EDGE_OK);
    assert_int_equal(edge_sys_publish(&sys, &ev), EDGE_EOVERFLOW);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.drops, 1u);
    assert_int_equal(stats.pending_high_water, 1u);
}

static void test_unsubscribe(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[2];
    edge_sys_t sys;
    edge_event_t ev = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 2u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_unsubscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_unsubscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_ENOENT);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_event_push_isr(&queue, &ev), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.event_count, 0);
}

/*
 * #176: a callback that unsubscribes must not swallow the event for the
 * subscriptions that follow it. `edge_sys_dispatch_events` walks the subscription
 * array in reverse precisely so that the left shift a removal causes cannot move an
 * unvisited subscription into a slot the loop has already passed - forward
 * iteration drops the last subscriber, which is invisible until someone relies on
 * two handlers for one event.
 */
/*
 * #162: the required list is **borrowed, not copied**, so it has to outlive the sys.
 * This pins the behaviour a product depends on when it uses a `static const` table,
 * and documents the trap for a caller that passes a stack frame: the list is read on
 * every validation, not captured at registration.
 */
static void test_required_list_is_borrowed_not_copied(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 7u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    static const uint32_t present[] = {7u};
    static const uint32_t absent[] = {8u};

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_required(&sys, present, 1u), EDGE_OK);
    assert_int_equal(edge_sys_validate_required(&sys), EDGE_OK);
    /* Repointing the sys at another table changes the answer: nothing was copied. */
    assert_int_equal(edge_sys_set_required(&sys, absent, 1u), EDGE_OK);
    assert_true(edge_sys_validate_required(&sys) < 0);
}

static void test_unsubscribe_during_dispatch_delivers_to_the_rest(void **state) {
    (void)state;
    fake_app_t a, b, c;
    make_app(&a, 1u, 1u);
    make_app(&b, 2u, 2u);
    make_app(&c, 3u, 3u);
    edge_module_t *apps[] = {&a.module, &b.module, &c.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[4];
    edge_sys_t sys;
    const edge_event_t event = {.id = EDGE_EVT_UART0_RX};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);
    assert_int_equal(edge_sys_init(&sys, apps, 3u), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 4u), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &a.module), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &b.module), EDGE_OK);
    assert_int_equal(edge_sys_subscribe(&sys, EDGE_EVT_UART0_RX, &c.module), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);

    /* The middle subscriber leaves while the event is being delivered. */
    b.unsubscribe_sys = &sys;
    b.unsubscribe_id = EDGE_EVT_UART0_RX;
    b.unsubscribe_app = &b.module;

    assert_int_equal(edge_event_push_isr(&queue, &event), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);

    assert_int_equal(b.unsubscribe_count, 1);
    /* Every subscription present when the dispatch began received the event. */
    assert_int_equal(a.event_count, 1);
    assert_int_equal(b.event_count, 1);
    assert_int_equal(c.event_count, 1);
}

static void test_suspend_resume(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_suspend_all(&sys), EDGE_OK);
    assert_int_equal(a.suspend_count, 1);
    assert_int_equal(a.module.suspended, 1u);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 0);
    assert_int_equal(edge_sys_resume_all(&sys), EDGE_OK);
    assert_int_equal(a.resume_count, 1);
    assert_int_equal(a.module.suspended, 0u);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(a.poll_count, 1);
    assert_int_equal(edge_sys_suspend_all(&sys), EDGE_OK);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);
    assert_int_equal(a.module.suspended, 0u);
}

static void test_idle_hook(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    a.module.period = 0u;
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    edge_sys_stats_t stats;

    g_idle_calls = 0;
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_set_idle(&sys, fake_idle, NULL), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(g_idle_calls, 1);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.idle_calls, 1u);
}

static void test_step_and_run_shutdown(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    g_shutdown_sys = &sys;
    g_shutdown_after_polls = 3;
    assert_int_equal(edge_sys_run(&sys), EDGE_OK);
    assert_int_equal(sys.state, EDGE_SYS_STOPPED);
    assert_int_equal(a.poll_count, 3);
    assert_int_equal(edge_sys_step(NULL), EDGE_ESTATE);
}

static void test_stats_reset(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_sys_t sys;
    edge_sys_stats_t stats;

    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_run_once(&sys), EDGE_OK);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.polls, 1u);
    assert_int_equal(edge_sys_stats_reset(&sys), EDGE_OK);
    assert_int_equal(edge_sys_stats_get(&sys, &stats), EDGE_OK);
    assert_int_equal(stats.polls, 0u);
}

static void test_new_api_argument_guards(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t pending[4];
    edge_event_t ev = {.id = EDGE_EVT_UART0_RX};
    edge_sys_t sys;

    assert_int_equal(edge_sys_bind_pending_queue(NULL, pending, 4u), EDGE_EINVAL);
    assert_int_equal(edge_sys_bind_pending_queue(&sys, NULL, 4u), EDGE_EINVAL);
    assert_int_equal(edge_sys_init(&sys, apps, 1u), EDGE_OK);
    assert_int_equal(edge_sys_bind_pending_queue(&sys, pending, 1u), EDGE_EINVAL);
    assert_int_equal(edge_sys_bind_pending_queue(&sys, pending, 4u), EDGE_OK);
    assert_int_equal(edge_sys_set_idle(NULL, NULL, NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_set_idle(&sys, NULL, NULL), EDGE_OK);
    assert_int_equal(edge_sys_publish(NULL, &ev), EDGE_EINVAL);
    assert_int_equal(edge_sys_publish(&sys, NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_publish(&sys, &ev), EDGE_ESTATE);
    assert_int_equal(edge_sys_unsubscribe(NULL, 1u, &a.module), EDGE_EINVAL);
    assert_int_equal(edge_sys_unsubscribe(&sys, 1u, NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_unsubscribe(&sys, 1u, &a.module), EDGE_EINVAL);
    assert_int_equal(edge_sys_idle(NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_stats_reset(NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_step(NULL), EDGE_ESTATE);
    assert_int_equal(edge_sys_run(NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_suspend_all(NULL), EDGE_ESTATE);
    assert_int_equal(edge_sys_resume_all(NULL), EDGE_ESTATE);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_suspend_all(&sys), EDGE_OK);
    assert_int_equal(edge_sys_suspend_all(&sys), EDGE_OK);
    assert_int_equal(edge_sys_resume_all(&sys), EDGE_OK);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);
    assert_int_equal(edge_sys_set_idle(&sys, NULL, NULL), EDGE_ESTATE);
}

static void test_sys_configure(void **state) {
    (void)state;
    fake_app_t a;
    make_app(&a, 1u, 1u);
    edge_module_t *apps[] = {&a.module};
    edge_event_t storage[4];
    edge_event_queue_t queue;
    edge_sys_subscription_t subs[4];
    edge_sys_t sys;
    static const uint32_t required[] = {1u};
    edge_clock_port_t clock = {.monotonic_ticks = NULL, .wall_time = NULL, .self = NULL};

    assert_int_equal(edge_event_queue_init(&queue, storage, 4u), EDGE_OK);

    /* NULL guards */
    assert_int_equal(edge_sys_configure(NULL, NULL), EDGE_EINVAL);
    assert_int_equal(edge_sys_configure(&sys, NULL), EDGE_EINVAL);

    const edge_sys_config_t config = {
        .apps = apps,
        .app_count = 1u,
        .required_ids = required,
        .required_count = 1u,
        .events = &queue,
        .subscriptions = subs,
        .subscription_capacity = 4u,
        .clock = &clock,
        .event_budget = 16u,
    };

    assert_int_equal(edge_sys_configure(&sys, &config), EDGE_OK);
    assert_int_equal(edge_sys_validate_required(&sys), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_power_off(&sys), EDGE_OK);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_OK);
}

/*
 * Every case resets the module's state **before** it runs. `cmocka_run_group_tests`
 * would run a setup once for the whole group, which is not enough: state leaks
 * between cases, and an end-of-test reset is skipped by the longjmp an assertion
 * takes - so it fails exactly when the state is dirty (#171).
 */
#define TEST(fn) cmocka_unit_test_setup(fn, reset_state)

/*
 * The suspend and resume sweeps in their failure paths, plus the pending check's branches. The
 * existing case exercises a clean sweep, so the half that matters operationally - an app whose
 * callback fails being isolated while only the first error is reported - was unexecuted.
 */
static void test_suspend_resume_and_pending(void **state) {
    (void)state;
    fake_app_t a;
    fake_app_t b;
    make_app(&a, 1u, 1u);
    make_app(&b, 2u, 2u);
    edge_module_t *apps[] = {&a.module, &b.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 2u), EDGE_OK);

    /* Before the system runs the sweeps are refused, and a null system is not pending. */
    assert_int_equal(edge_sys_suspend_all(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_resume_all(&sys), EDGE_ESTATE);
    assert_false(edge_sys_pending(NULL));

    assert_int_equal(edge_sys_start(&sys), EDGE_OK);

    /* A clean sweep suspends both, and resuming brings them back. */
    assert_int_equal(edge_sys_suspend_all(&sys), EDGE_OK);
    assert_int_equal(a.suspend_count, 1);
    assert_int_equal(b.suspend_count, 1);
    assert_int_equal(edge_sys_resume_all(&sys), EDGE_OK);
    assert_int_equal(a.resume_count, 1);
    assert_int_equal(b.resume_count, 1);

    /* An app whose suspend fails is isolated, and its error is the one reported. */
    b.suspend_rc = EDGE_EIO;
    assert_int_equal(edge_sys_suspend_all(&sys), EDGE_EIO);
    assert_true(sys.stats.isolated >= 1u);
    assert_int_equal(edge_sys_resume_all(&sys), EDGE_OK);

    /* Pending: a pending count answers true, and so does an app whose next due time has passed. */
    sys.pending_count = 1u;
    assert_true(edge_sys_pending(&sys));
    sys.pending_count = 0u;
    a.module.failed = 0u;
    a.module.suspended = 0u;
    a.module.next_due = 0u;
    assert_true(edge_sys_pending(&sys));
}

int main(void) {

    const struct CMUnitTest tests[] = {
        TEST(test_start_sorts_by_priority_then_id),
        TEST(test_validation),
        TEST(test_required_and_lifecycle),
        TEST(test_event_routing_and_bound),
        TEST(test_periodic_scheduler),
        TEST(test_event_budget_and_stats),
        TEST(test_fault_isolation),
        TEST(test_power_off_failure),
        TEST(test_power_off_runs_in_reverse_order),
        TEST(test_wrapper_and_guards),
        TEST(test_argument_and_state_guards),
        TEST(test_required_missing),
        TEST(test_dispatch_event_failure_and_pop_error),
        TEST(test_execution_budget),
        TEST(test_deinit_stops_scheduler_only),
        TEST(test_meter_wrapper),
        TEST(test_publish_deferred_queue),
        TEST(test_publish_overflow_and_binding),
        TEST(test_unsubscribe),
        TEST(test_unsubscribe_during_dispatch_delivers_to_the_rest),
        TEST(test_required_list_is_borrowed_not_copied),
        TEST(test_suspend_resume),
        TEST(test_suspend_resume_and_pending),
        TEST(test_idle_hook),
        TEST(test_step_and_run_shutdown),
        TEST(test_stats_reset),
        TEST(test_new_api_argument_guards),
        TEST(test_sys_configure),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
