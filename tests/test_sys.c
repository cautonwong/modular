#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "edge/event.h"
#include "edge/events.h"
#include "example/sys.h"

typedef struct fake_app {
    edge_module_t module;
    int init_count;
    int poll_count;
    int event_count;
    int off_count;
    int deinit_count;
    edge_status_t init_rc;
    edge_status_t poll_rc;
    edge_status_t event_rc;
    edge_status_t off_rc;
} fake_app_t;

static uint64_t g_now;
static int g_order[8];
static size_t g_order_count;

static uint64_t fake_clock(void *self) {
    (void)self;
    return g_now;
}

static fake_app_t *as_fake(edge_module_t *m) {
    return (fake_app_t *)m->private_data;
}

static edge_status_t fake_init(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->init_count;
    if (g_order_count < 8u)
        g_order[g_order_count++] = (int)m->module_id;
    return a->init_rc;
}

static edge_status_t fake_poll(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->poll_count;
    return a->poll_rc;
}

static edge_status_t fake_event(edge_module_t *m, const edge_event_t *e) {
    (void)e;
    fake_app_t *a = as_fake(m);
    ++a->event_count;
    return a->event_rc;
}

static edge_status_t fake_off(edge_module_t *m) {
    fake_app_t *a = as_fake(m);
    ++a->off_count;
    return a->off_rc;
}

static edge_status_t fake_deinit(edge_module_t *m) {
    ++as_fake(m)->deinit_count;
    return EDGE_OK;
}

static void make_app(fake_app_t *a, uint32_t id, uint32_t priority) {
    *a = (fake_app_t){0};
    a->module = (edge_module_t){
        .module_id = id,
        .priority = priority,
        .period = 1u,
        .budget = 0u,
        .init = fake_init,
        .poll = fake_poll,
        .on_event = fake_event,
        .power_off = fake_off,
        .deinit = fake_deinit,
        .private_data = a,
    };
}

static void test_init_and_order(void **state) {
    (void)state;
    fake_app_t a, b, c;
    make_app(&a, 30u, 10u);
    make_app(&b, 20u, 10u);
    make_app(&c, 10u, 20u);
    edge_module_t *apps[] = {&a.module, &b.module, &c.module};
    edge_sys_t sys;
    g_order_count = 0u;

    assert_int_equal(edge_sys_init(&sys, apps, 3u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(g_order[0], 20);
    assert_int_equal(g_order[1], 30);
    assert_int_equal(g_order[2], 10);
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
    assert_int_equal(a.deinit_count, 1);
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

static void test_init_rollback_and_power_failure(void **state) {
    (void)state;
    fake_app_t a, b;
    make_app(&a, 1u, 1u);
    make_app(&b, 2u, 2u);
    b.init_rc = EDGE_EIO;
    edge_module_t *apps[] = {&a.module, &b.module};
    edge_sys_t sys;

    assert_int_equal(edge_sys_init(&sys, apps, 2u), EDGE_OK);
    assert_int_equal(edge_sys_start(&sys), EDGE_EIO);
    assert_int_equal(a.deinit_count, 1);
    assert_int_equal(a.module.initialized, 0u);
    assert_int_equal(b.module.failed, 1u);

    make_app(&a, 1u, 1u);
    a.off_rc = EDGE_EIO;
    apps[0] = &a.module;
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
    assert_int_equal(edge_sys_start(&sys), EDGE_OK);
    assert_int_equal(edge_sys_bind_event_queue(&sys, &queue, subs, 1u), EDGE_ESTATE);
    assert_int_equal(edge_sys_start(&sys), EDGE_ESTATE);
    assert_int_equal(edge_sys_deinit(&sys), EDGE_ESTATE);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_and_order),
        cmocka_unit_test(test_validation),
        cmocka_unit_test(test_required_and_lifecycle),
        cmocka_unit_test(test_event_routing_and_bound),
        cmocka_unit_test(test_periodic_scheduler),
        cmocka_unit_test(test_event_budget_and_stats),
        cmocka_unit_test(test_fault_isolation),
        cmocka_unit_test(test_init_rollback_and_power_failure),
        cmocka_unit_test(test_wrapper_and_guards),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
